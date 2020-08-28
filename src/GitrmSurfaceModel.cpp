#include <netcdf>
#include "GitrmSurfaceModel.hpp"
#include "GitrmInputOutput.hpp"

GitrmSurfaceModel::GitrmSurfaceModel(GitrmMesh& gm, std::string ncFile):
  gm(gm), mesh(gm.mesh),  ncFile(ncFile) { 
  initSurfaceModelData(ncFile, true);

  numSurfMaterialFaces = gm.nSurfMaterialFaces;
  surfaceAndMaterialOrderedIds = gm.getSurfaceAndMaterialOrderedIds();
  nDetectSurfaces = gm.nDetectSurfaces;
  detectorMeshFaceOrderedIds = gm.getDetectorMeshFaceOrderedIds();
  bdryFaceMaterialZs = gm.getBdryFaceMaterialZs();
  bdryFaceOrderedIds = gm.getBdryFaceOrderedIds();
  surfMatGModelSeqNums = gm.getSurfMatGModelSeqNums();
  rank = mesh.comm()->rank();
}


void GitrmSurfaceModel::initSurfaceModelData(std::string ncFile, bool debug) {
  getConfigData(ncFile);
  if(debug)
    std::cout << "Done reading data \n";
  int ndmIds = gm.getNumDetectorModelIds();

  nDistEsurfaceModel =
     nEnSputtRefDistIn * nAngSputtRefDistIn * nEnSputtRefDistOut;
  nDistEsurfaceModelRef =
     nEnSputtRefDistIn * nAngSputtRefDistIn * nEnSputtRefDistOutRef;
  nDistAsurfaceModel =
     nEnSputtRefDistIn * nAngSputtRefDistIn * nAngSputtRefDistOut;
  if(debug)
    std::cout << "prepareSurfaceModelData \n";

  prepareSurfaceModelData();

  //if(gitrm::SURFACE_FLUX_EA > 0) {
  dEdist = (enDist - en0Dist)/nEnDist;
  dAdist = (angDist - ang0Dist)/nAngDist;

  auto nDist = ndmIds * nEnDist * nAngDist;
  if(debug)
    printf(" nEdist %d nAdist %d #nDetModelIds %d nDist %d\n", nEnDist, nAngDist, ndmIds, nDist);
  energyDistribution = o::Write<o::Real>(nDist, 0, "surfEnDist"); // one per detFace
  sputtDistribution = o::Write<o::Real>(nDist, 0, "surfSputtDist"); // one per detFace
  reflDistribution = o::Write<o::Real>(nDist, 0, "surfReflDist"); //one per detFace
  auto nf = mesh.nfaces();
  sideIsExposed = o::mark_exposed_sides(&mesh);
  sumPtclStrike = o::Write<o::LO>(nf, 0, "sumPtclStrike");
  sputtYldCount = o::Write<o::LO>(nf, 0, "sputtYldCount");
  sumWtStrike = o::Write<o::Real>(nf, 0, "sumWtStrike");
  grossDeposition = o::Write<o::Real>(nf, 0, "grossDeposition");
  grossErosion = o::Write<o::Real>(nf, 0, "grossErosion");
  aveSputtYld = o::Write<o::Real>(nf, 0, "aveSputtYld");
  //mesh.add_tag<o::LO>(o::FACE, "SumParticlesStrike", 1, o::Read<o::Int>(nf,0, "SumParticlesStrike"));
}

void GitrmSurfaceModel::make2dCDF(const int nX, const int nY, const int nZ, 
   const o::HostWrite<o::Real>& distribution, o::HostWrite<o::Real>& cdf) {
  OMEGA_H_CHECK(distribution.size() == nX*nY*nZ);
  OMEGA_H_CHECK(cdf.size() == nX*nY*nZ);
  int index = 0;
  for(int i=0;i<nX;i++) {
    for(int j=0;j<nY;j++) {
      for(int k=0;k<nZ;k++) {
        index = i*nY*nZ + j*nZ + k;
        if(k==0)
          cdf[index] = distribution[index];
        else
          cdf[index] = cdf[index-1] + distribution[index];
      }  
    }  
  }
  for(int i=0;i<nX;i++) {
    for(int j=0;j<nY;j++) {
      if(cdf[i*nY*nZ + (j+1)*nZ - 1] > 0) {
        for(int k=0;k<nZ;k++) {  
          index = i*nY*nZ + j*nZ + k;
          cdf[index] = cdf[index] / cdf[index-k+nZ-1];
        }
      }
    }
  }
}

//TODO make DEVICE
o::Real GitrmSurfaceModel::interp1dUnstructured(const o::Real samplePoint, 
  const int nx, const o::Real max_x, const o::Real* data, int& lowInd) {
  int done = 0;
  int low_index = 0;
  o::Real value = 0;
  for(int i=0;i<nx;i++) {
    if(done == 0) {
      if(samplePoint < data[i]) {
        done = 1;
        low_index = i-1;
      }   
    }
  }
  value = ((data[low_index+1] - samplePoint)*low_index*max_x/nx
        + (samplePoint - data[low_index])*(low_index+1)*max_x/nx)/
          (data[low_index+1]- data[low_index]);
  lowInd = low_index;
  if(low_index < 0) {
    lowInd = 0;
    if(samplePoint > 0) {
      value = samplePoint;
    } else {
      value = 0;
    }
  }
  if(low_index >= nx) {
    lowInd = nx-1;
    value = max_x;
  }
  return value;
}

//TODO convert to device function
void GitrmSurfaceModel::regrid2dCDF(const int nX, const int nY, const int nZ, 
   const o::HostWrite<o::Real>& xGrid, const int nNew, const o::Real maxNew, 
   const o::HostWrite<o::Real>& cdf, o::HostWrite<o::Real>& cdf_regrid) {
  int lowInd = 0;
  int index = 0;
  float spline = 0.0;
  for(int i=0;i<nX;i++) {
    for(int j=0;j<nY;j++) {
      for(int k=0;k<nZ;k++) {
        index = i*nY*nZ + j*nZ + k;
        spline = interp1dUnstructured(xGrid[k], nNew, maxNew, 
                  &(cdf.data()[index-k]), lowInd);
        if(isnan(spline) || isinf(spline)) 
          spline = 0.0;
        cdf_regrid[index] = spline;  
      }  
    }
  }
}

void GitrmSurfaceModel::prepareSurfaceModelData() {

  o::Write<o::Real> enLogSputtRefCoef_w(nEnSputtRefCoeff, 0, "enLogSputtRefCoef");
  auto enSputtRefCft = enSputtRefCoeff;
  o::parallel_for(nEnSputtRefCoeff, OMEGA_H_LAMBDA(const o::LO& i) {
    enLogSputtRefCoef_w[i] = log10(enSputtRefCft[i]);
  },"kernel_nEnSputtRefCoeff");
  enLogSputtRefCoef = o::Reals(enLogSputtRefCoef_w);
  auto enSputtRefDIn = enSputtRefDistIn;
  o::Write<o::Real> enLogSputtRefDistIn_w(nEnSputtRefDistIn, 0, "enLogSputtRefDistIn");
  o::parallel_for(nEnSputtRefDistIn, OMEGA_H_LAMBDA(const o::LO& i) {
    enLogSputtRefDistIn_w[i] = log10(enSputtRefDIn[i]);
  },"kernel_nEnSputtRefDistIn");
  enLogSputtRefDistIn = o::Reals(enLogSputtRefDistIn_w);
  o::HostWrite<o::Real>enLogSputtRefDistIn_h(enLogSputtRefDistIn_w);
  
  o::Write<o::Real> energyDistGrid01_w(nEnSputtRefDistOut, 0, "energyDistGrid01");
  auto nEnSputtRefDOut = nEnSputtRefDistOut;
  o::parallel_for(nEnSputtRefDOut, OMEGA_H_LAMBDA(const o::LO& i) {
    energyDistGrid01_w[i] = i * 1.0 / nEnSputtRefDOut;
  },"kernel_nEnSputtRefDOut");
  energyDistGrid01 = o::Reals(energyDistGrid01_w);
  o::HostWrite<o::Real>energyDistGrid01_h(energyDistGrid01_w);

  auto nEnSputtRefDORef = nEnSputtRefDistOutRef;
  o::Write<o::Real> energyDistGrid01Ref_w(nEnSputtRefDORef, 0, "energyDistGrid01Ref");
  o::parallel_for(nEnSputtRefDORef, OMEGA_H_LAMBDA(const o::LO& i) {
    energyDistGrid01Ref_w[i] = i * 1.0 / nEnSputtRefDORef;
  },"kernel_nEnSputtRefDORef");
  energyDistGrid01Ref = o::Reals(energyDistGrid01Ref_w);
  o::HostWrite<o::Real>energyDistGrid01Ref_h(energyDistGrid01Ref_w);

  auto nAngSputtRefDOut = nAngSputtRefDistOut;
  o::Write<o::Real> angleDistGrid01_w(nAngSputtRefDOut, 0, "angleDistGrid01");
  o::parallel_for(nAngSputtRefDOut, OMEGA_H_LAMBDA(const o::LO& i) {
    angleDistGrid01_w[i] = i * 1.0 / nAngSputtRefDOut;
  },"kernel_nAngSputtRefDOut");
  angleDistGrid01 = o::Reals(angleDistGrid01_w);
  o::HostWrite<o::Real>angleDistGrid01_h(angleDistGrid01_w);

  printf("Making CDFs\n"); 
  o::HostWrite<o::Real>enDist_CDF_Y(enDist_Y.size(), "enDist_CDF_Y");
  o::HostWrite<o::Real>enDist_Y_h(o::deep_copy(enDist_Y));
  make2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nEnSputtRefDistOut,
   enDist_Y_h, enDist_CDF_Y);

  o::HostWrite<o::Real>angPhiDist_Y_h(o::deep_copy(angPhiDist_Y));
  o::HostWrite<o::Real>angPhiDist_CDF_Y(angPhiDist_Y.size(), "angPhiDist_CDF_Y");
  make2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angPhiDist_Y_h, angPhiDist_CDF_Y);

  o::HostWrite<o::Real>angThetaDist_Y_h(o::deep_copy(angThetaDist_Y));
  o::HostWrite<o::Real>angThetaDist_CDF_Y(angThetaDist_Y.size(), "angThetaDist_CDF_Y");
  make2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angThetaDist_Y_h, angThetaDist_CDF_Y);

  o::HostWrite<o::Real>enDist_R_h(o::deep_copy(enDist_R));
  o::HostWrite<o::Real>enDist_CDF_R(enDist_R.size(), "enDist_CDF_R");
  make2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nEnSputtRefDistOutRef,
   enDist_R_h, enDist_CDF_R);

  o::HostWrite<o::Real>angPhiDist_R_h(o::deep_copy(angPhiDist_R));
  o::HostWrite<o::Real>angPhiDist_CDF_R(angPhiDist_R.size(), "angPhiDist_CDF_R");
  make2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angPhiDist_R_h, angPhiDist_CDF_R);

  o::HostWrite<o::Real>angThetaDist_R_h(o::deep_copy(angThetaDist_R));
  o::HostWrite<o::Real>angThetaDist_CDF_R(angThetaDist_R.size(), "angThetaDist_CDF_R");
  make2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angThetaDist_R_h, angThetaDist_CDF_R);
  
  printf("Making regrid CDFs\n"); 
  o::HostRead<o::Real> angPhiSputtRefDistOut_h(o::deep_copy(angPhiSputtRefDistOut));
  o::HostWrite<o::Real>angPhiDist_CDF_Y_regrid_h(angPhiDist_CDF_Y.size(), 
      "angPhiDist_CDF_Y_regrid");
  regrid2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angleDistGrid01_h, nAngSputtRefDistOut,
   angPhiSputtRefDistOut_h[nAngSputtRefDistOut - 1],
   angPhiDist_CDF_Y, angPhiDist_CDF_Y_regrid_h);
  angPhiDist_CDF_Y_regrid = o::Reals(o::Write<o::Real>(angPhiDist_CDF_Y_regrid_h));

  o::HostRead<o::Real> angThetaSputtRefDistOut_h(o::deep_copy(angThetaSputtRefDistOut));
  o::HostWrite<o::Real>angThetaDist_CDF_Y_regrid_h(angThetaDist_CDF_Y.size(), 
      "angThetaDist_CDF_Y_regrid");
  regrid2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angleDistGrid01_h, nAngSputtRefDistOut, angThetaSputtRefDistOut_h[nAngSputtRefDistOut - 1],
   angThetaDist_CDF_Y, angThetaDist_CDF_Y_regrid_h);
  angThetaDist_CDF_Y_regrid = o::Reals(o::Write<o::Real>(angThetaDist_CDF_Y_regrid_h));

  o::HostRead<o::Real> enSputtRefDistOut_h(o::deep_copy(enSputtRefDistOut));
  o::HostWrite<o::Real>enDist_CDF_Y_regrid_h(enDist_CDF_Y.size(), "enDist_CDF_Y_regrid");
  regrid2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nEnSputtRefDistOut,
   energyDistGrid01_h, nEnSputtRefDistOut, enSputtRefDistOut_h[nEnSputtRefDistOut - 1],
   enDist_CDF_Y, enDist_CDF_Y_regrid_h);
  enDist_CDF_Y_regrid = o::Reals(enDist_CDF_Y_regrid_h);

  o::HostWrite<o::Real>angPhiDist_CDF_R_regrid_h(angPhiDist_CDF_R.size(), 
      "angPhiDist_CDF_R_regrid");
  regrid2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angleDistGrid01_h, nAngSputtRefDistOut,
   angPhiSputtRefDistOut_h[nAngSputtRefDistOut - 1],
   angPhiDist_CDF_R, angPhiDist_CDF_R_regrid_h);
  angPhiDist_CDF_R_regrid = o::Reals(angPhiDist_CDF_R_regrid_h);

  o::HostWrite<o::Real>angThetaDist_CDF_R_regrid_h(angThetaDist_CDF_R.size(), 
      "angThetaDist_CDF_R_regrid");
  regrid2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nAngSputtRefDistOut,
   angleDistGrid01_h, nAngSputtRefDistOut,
   angThetaSputtRefDistOut_h[nAngSputtRefDistOut - 1],
   angThetaDist_CDF_R, angThetaDist_CDF_R_regrid_h);
  angThetaDist_CDF_R_regrid = o::Reals(angThetaDist_CDF_R_regrid_h);

  o::HostRead<o::Real>enSputtRefDistOutRef_h(o::deep_copy(enSputtRefDistOutRef));
  o::HostWrite<o::Real>enDist_CDF_R_regrid_h(enDist_CDF_R.size(), "enDist_CDF_R_regrid");
  regrid2dCDF(nEnSputtRefDistIn, nAngSputtRefDistIn, nEnSputtRefDistOutRef,
   energyDistGrid01Ref_h, nEnSputtRefDistOutRef,
   enSputtRefDistOutRef_h[nEnSputtRefDistOutRef - 1],
   enDist_CDF_R, enDist_CDF_R_regrid_h);
  enDist_CDF_R_regrid = o::Reals(enDist_CDF_R_regrid_h);
}

void GitrmSurfaceModel::getConfigData(std::string ncFileName) {
  //TODO get from config file
  //collect data for analysis/plot
  nEnDist = 50; //100 //TODO FIXME
  en0Dist = 0.0;
  enDist = 1000.0;
  nAngDist = 45; //90
  ang0Dist = 0.0;
  angDist = 90.0; 
  //from NC file ftridynSelf.nc
  fileString = ncFileName;//"ftridynSelf.nc";
  nEnSputtRefCoeffStr = "nE";
  nAngSputtRefCoeffStr = "nA";
  nEnSputtRefDistInStr = "nE";
  nAngSputtRefDistInStr = "nA";
  nEnSputtRefDistOutStr = "nEdistBins";
  nEnSputtRefDistOutRefStr = "nEdistBinsRef";
  nAngSputtRefDistOutStr = "nAdistBins";
  enSputtRefCoeffStr = "E";
  angSputtRefCoeffStr = "A";
  enSputtRefDistInStr = "E";
  angSputtRefDistInStr = "A";
  enSputtRefDistOutStr = "eDistEgrid";
  enSputtRefDistOutRefStr = "eDistEgridRef";
  angPhiSputtRefDistOutStr = "phiGrid";
  angThetaSputtRefDistOutStr = "thetaGrid";
  sputtYldStr = "spyld";
  reflYldStr = "rfyld";
  enDistYStr = "energyDist";
  angPhiDistYStr = "cosXDist";
  angThetaDistYStr = "cosYDist";
  enDistRStr = "energyDistRef";
  angPhiDistRStr = "cosXDistRef";
  angThetaDistRStr = "cosYDistRef";

  std::vector<std::string> ds{nEnSputtRefCoeffStr, nAngSputtRefCoeffStr,
   nEnSputtRefDistInStr, nAngSputtRefDistInStr, nEnSputtRefDistOutStr, 
   nEnSputtRefDistOutRefStr, nAngSputtRefDistOutStr};
  std::vector<int> dd{nEnSputtRefCoeff, nAngSputtRefCoeff, nEnSputtRefDistIn, 
   nAngSputtRefDistIn, nEnSputtRefDistOut, nEnSputtRefDistOutRef, nAngSputtRefDistOut};
  std::cout << " getSurfaceModelData \n"; 
  //grids are read as separate data, since grid association with data is complex.
  auto f = fileString;
  getSurfaceModelData(f, sputtYldStr, ds, {0,1}, sputtYld);
  getSurfaceModelData(f, reflYldStr, ds, {0,1}, reflYld);
  getSurfaceModelData(f, enSputtRefCoeffStr, ds, {0}, enSputtRefCoeff,
    &nEnSputtRefCoeff);
  getSurfaceModelData(f, angSputtRefCoeffStr, ds, {1}, angSputtRefCoeff,
    &nAngSputtRefCoeff);
  getSurfaceModelData(f, enSputtRefDistInStr, ds, {2}, enSputtRefDistIn,
    &nEnSputtRefDistIn);
  getSurfaceModelData(f, angSputtRefDistInStr, ds, {3}, angSputtRefDistIn,
    &nAngSputtRefDistIn);
  //TODO nEnSputtRefDistInStr not used
  getSurfaceModelData(f, angPhiDistYStr, ds, {0,1,6}, angPhiDist_Y);
  getSurfaceModelData(f, angThetaDistYStr, ds, {0,1,6}, angThetaDist_Y);
  getSurfaceModelData(f, angPhiDistRStr, ds, {0,1,6}, angPhiDist_R);
  getSurfaceModelData(f, angThetaDistRStr, ds, {0,1,6}, angThetaDist_R);
  o::Reals enDist_Y_temp;
  //enDist_Y = enDist_Y_temp;
  getSurfaceModelData(f, enDistYStr, ds, {0,1,4}, enDist_Y);//_temp);
  getSurfaceModelData(f, enDistRStr, ds, {0,1,5}, enDist_R);
  getSurfaceModelData(f, enSputtRefDistOutStr, ds, {4}, enSputtRefDistOut,
    &nEnSputtRefDistOut);
  getSurfaceModelData(f, enSputtRefDistOutRefStr, ds, {5}, enSputtRefDistOutRef,
    &nEnSputtRefDistOutRef);
  getSurfaceModelData(f, angPhiSputtRefDistOutStr, ds, {6}, angPhiSputtRefDistOut,
    &nAngSputtRefDistOut);
  getSurfaceModelData(f, angThetaSputtRefDistOutStr, ds, {6}, 
    angThetaSputtRefDistOut, &nAngSputtRefDistOut);
}

void GitrmSurfaceModel::getSurfaceModelData(const std::string fileName,
   const std::string dataName, const std::vector<std::string>& shapeNames,
   const std::vector<int> shapeInds, o::Reals& data, int* size) {
  bool debug = true;
  if(debug)
    std::cout << " reading " << dataName << " \n";
  std::vector<std::string> shapes; 
  for(auto j: shapeInds) {
    shapes.push_back(shapeNames[j]);
  }
  //grid not read along with data
  Field3StructInput fs({dataName},{},shapes);
  readInputDataNcFileFS3(fileName, fs, false);
  data = o::Reals(o::Write<o::Real>(fs.data));
  //only first
  if(size) {
    *size = fs.getIntValueOf(shapeNames[shapeInds[0]]);//fs.getNumGrids(j); 
    if(debug)
      std::cout<<" size "<< *size <<" "<<shapeNames[shapeInds[0]]<<"\n";
  }
}

//This won't work for partitioned mesh of non-full-buffer
//TODO move to IO file
//Call at the end of simulation
void GitrmSurfaceModel::writeSurfaceDataFile(std::string fileName) const {
  printf("Writing surface model output \n");
  o::HostWrite<o::Real> energyDist_in(energyDistribution);
  o::HostWrite<o::Real> reflDist_in(reflDistribution);
  o::HostWrite<o::Real> sputtDist_in(sputtDistribution);
  o::HostWrite<o::Real> energyDist_h(energyDistribution.size(), "energyDist_h");
  o::HostWrite<o::Real> reflDist_h(reflDistribution.size(), "reflDist_h");
  o::HostWrite<o::Real> sputtDist_h(sputtDistribution.size(), "sputtDist_h");

  //FIXME this is only for full buffer partitioning.
  MPI_Reduce(energyDist_in.data(), energyDist_h.data(), 
    energyDist_h.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(reflDist_in.data(), reflDist_h.data(), 
    reflDist_h.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(sputtDist_in.data(), sputtDist_h.data(),
    sputtDist_h.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  if(rank) {
    return;
  }
 
  netCDF::NcFile ncf(fileName, netCDF::NcFile::replace);
  int ndmIds = gm.getNumDetectorModelIds();
  netCDF::NcDim ncs = ncf.addDim("nSurfaces", ndmIds);
  std::vector<netCDF::NcDim> dims;
  dims.push_back(ncs);
  netCDF::NcDim nEn = ncf.addDim("nEnergies", nEnDist);
  dims.push_back(nEn);
  netCDF::NcDim nAng = ncf.addDim("nAngles", nAngDist);
  dims.push_back(nAng);
  netCDF::NcVar grossDep = ncf.addVar("grossDeposition", netCDF::ncDouble, ncs);
  grossDep.putVar(grossDeposition.data());
  netCDF::NcVar grossEro = ncf.addVar("grossErosion", netCDF::ncDouble, ncs);
  grossEro.putVar(grossErosion.data());
  netCDF::NcVar aveSpyl = ncf.addVar("aveSpyl", netCDF::ncDouble, ncs);
  aveSpyl.putVar(aveSputtYld.data());
  netCDF::NcVar spylCounts = ncf.addVar("spylCounts", netCDF::ncInt, ncs);
  spylCounts.putVar(sputtYldCount.data());
  //NcVar surfNum = ncf.addVar("surfaceNumber", ncInt, ncs);
  //surfNum.putVar(&surfIds[0]);
  netCDF::NcVar nPtlcsStrike = ncf.addVar("sumParticlesStrike", netCDF::ncInt, ncs);
  nPtlcsStrike.putVar(sumPtclStrike.data());
  netCDF::NcVar nWtStrike =  ncf.addVar("sumWeightStrike", netCDF::ncDouble, ncs);
  nWtStrike.putVar(sumWtStrike.data());
  netCDF::NcVar surfEDist = ncf.addVar("surfEDist", netCDF::ncDouble, dims);
  surfEDist.putVar(energyDist_h.data());
  netCDF::NcVar surfReflDist = ncf.addVar("surfReflDist", netCDF::ncDouble, dims);
  surfReflDist.putVar(reflDist_h.data());
  netCDF::NcVar surfSputtDist = ncf.addVar("surfSputtDist", netCDF::ncDouble, dims);
  surfSputtDist.putVar(sputtDist_h.data());
}
