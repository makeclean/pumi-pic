#ifndef GITRM_IONIZE_RECOMBINE_HPP
#define GITRM_IONIZE_RECOMBINE_HPP

#include "GitrmParticles.hpp"
#include <cstdlib>
#include <ctime>
#include <fstream>

class GitrmIonizeRecombine {
public:
  GitrmIonizeRecombine(const std::string &fName, bool charged=true);
  void initIonizeRecombRateData(const std::string &, int debug=0);
  bool chargedPtclTracking = true;
  o::Real ionizeTempGridMin = 0;
  o::Real ionizeDensGridMin = 0;
  o::Real ionizeTempGridDT = 0;
  o::Real ionizeDensGridDn = 0;
  o::LO ionizeTempGridN = 0;
  o::LO ionizeDensGridN = 0;
  o::Real recombTempGridMin = 0;
  o::Real recombDensGridMin = 0;
  o::Real recombTempGridDT = 0;
  o::Real recombDensGridDn = 0;
  o::LO recombTempGridN = 0;
  o::LO recombDensGridN = 0;
  
  o::Reals ionizationRates;
  o::Reals recombinationRates;
  o::Reals gridTempIonize;
  o::Reals gridDensIonize;
  o::Reals gridTempRec;
  o::Reals gridDensRec;
};

// input temperature is in log, but density is not.
// stored as 3component data, but interpolated from 2D grid in log scale
OMEGA_H_DEVICE o::Real interpolateRateCoeff(const o::Reals &data, 
   const o::Reals &gridTemp, const o::Reals &gridDens, const o::Real tem,
   const o::Real dens, const o::Real gridT0, const o::Real gridD0, 
   const o::Real dT, const o::Real dD, const o::LO nT,  const o::LO nD, o::LO charge) {

  int debug = 0;
  o::LO indT = floor( (log10(tem) - gridT0)/dT );
  o::LO indN = floor( (log10(dens) - gridD0)/dD );
  if(indT < 0 || indT > nT-2)
    indT = 0;
  if(indN < 0 || indN > nD-2)
    indN = 0;

  auto gridTnext = gridTemp[indT+1];
  o::Real aT = pow(10.0, gridTnext) - tem;
  auto gridT = gridTemp[indT];
  o::Real bT = tem - pow(10.0, gridT);
  o::Real abT = aT+bT;
  auto gridDnext = gridDens[indN+1];
  o::Real aN = pow(10.0, gridDnext) - dens;
  auto gridD = gridDens[indN];
  o::Real bN = dens - pow(10.0, gridD);
  o::Real abN = aN + bN;
  if(debug)
    printf("InterpRate: gridTnext %g pow(10.0, gridTnext) %g  aT %g bT %g "
        "tem %g abT %g aN %g bN %g dens %g abN %g \n", 
         gridTnext, pow(10.0, gridTnext), aT, bT, tem, abT, aN, bN, dens, abN);
  o::Real fx_z1 = (aN*pow(10.0, data[charge*nT*nD + indT*nD + indN]) 
          + bN*pow(10.0, data[charge*nT*nD + indT*nD + indN + 1]))/abN;
  
  o::Real fx_z2 = (aN*pow(10.0, data[charge*nT*nD + (indT+1)*nD + indN]) 
          + bN*pow(10.0, data[charge*nT*nD + (indT+1)*nD + indN+1]))/abN;
  o::Real RClocal = (aT*fx_z1+bT*fx_z2)/abT;
  
  if(debug)
    printf("  interpRate: t %g n %g gridT0 %g log10(tem) %g indT %d  gridD0 %g "
      "log10(dens) %g indN %d dT %g dD %g nT %d nD %d charge %d fx_z1 %g fx_z2 %g "
      "RClocal(fxz) %g\n", tem,dens,gridT0, log10(tem), indT, gridD0, log10(dens),
      indN, dT,dD,nT,nD,charge, fx_z1, fx_z2, RClocal);

  o::Real rate;
  if(o::are_close(tem,0) || o::are_close(dens, 0))
    rate = 1.0e12;
  else {
    auto eq = p::almost_equal(RClocal*dens, 0, 1e-20, 1e-20); 
    if(eq)
      printf("tem %g dens %g RClocal %g\n", tem, dens, RClocal);
   // OMEGA_H_CHECK(!eq);
    rate = 1/(RClocal*dens);
  }
  OMEGA_H_CHECK(!isnan(rate));
  return rate;
}

inline void gitrm_ionize(PS* ptcls, const GitrmIonizeRecombine& gir, 
  GitrmParticles& gp, const GitrmMesh& gm, const o::LOs& elm_ids, int debug = 0) {
  if(!gir.chargedPtclTracking)
    return;
  if(debug) {
    MPI_Barrier(MPI_COMM_WORLD);
    printf("Ionization \n");
  }
  auto& mesh = gm.mesh;
  auto use2DRatesData = USE_2DREADIN_IONI_REC_RATES;
  auto densIon_d = gm.getDensIon();
  auto temIon_d = gm.getTemIon();
  auto x0Dens = gm.densIonX0;
  auto z0Dens = gm.densIonZ0;
  auto nxDens = gm.densIonNx;
  auto nzDens = gm.densIonNz;
  auto dxDens = gm.densIonDx;
  auto dzDens = gm.densIonDz;
  auto x0Temp = gm.tempIonX0;
  auto z0Temp = gm.tempIonZ0;
  auto nxTemp = gm.tempIonNx;
  auto nzTemp = gm.tempIonNz;
  auto dxTemp = gm.tempIonDx;
  auto dzTemp = gm.tempIonDz;

  const bool useCudaRnd = gp.useCudaRnd;
  auto* cuStates =  gp.cudaRndStates;
  
  const int useGitrRnd = gp.useGitrRndNums;
  //#ifdef useGitrRndNums
  if(!gp.ranIonization)
    gp.ranIonization = true; //logging, not rnd
  const auto& testGitrPtclStepData = gp.testGitrPtclStepData;
  const auto testGDof = gp.testGitrStepDataDof;
  const auto testGNT = gp.testGitrStepDataNumTsteps;
  const auto testGIind = gp.testGitrDataIoniRandInd;
  const auto iTimeStep = iTimePlusOne - 1;
  if(useGitrRnd)
    OMEGA_H_CHECK(gp.testGitrOptIoniRec);
  else
    OMEGA_H_CHECK(!gp.testGitrOptIoniRec);
  //#endif

  auto& xfaces_d = gp.wallCollisionFaceIds;
  auto dt = gp.timeStep;
  auto gridT0 = gir.ionizeTempGridMin;
  auto gridD0 = gir.ionizeDensGridMin;
  auto dTem = gir.ionizeTempGridDT;
  auto dDens = gir.ionizeDensGridDn;
  auto nTRates = gir.ionizeTempGridN;
  auto nDRates = gir.ionizeDensGridN;
  const auto& iRates = gir.ionizationRates; 
  const auto& gridTemp = gir.gridTempIonize;
  const auto& gridDens = gir.gridDensIonize;
  const auto maxCharge = gitrm::maxCharge;
  const auto coords = mesh.coords();
  const auto mesh2verts = mesh.ask_elem_verts();
  const auto tIonVtx = mesh.get_array<o::Real>(o::VERT, "IonTempVtx");
  const auto densVtx = mesh.get_array<o::Real>(o::VERT, "IonDensityVtx"); 

  auto pid_ps = ptcls->get<PTCL_ID>();
  auto pid_ps_global = ptcls->get<PTCL_ID_GLOBAL>();
  auto new_pos = ptcls->get<PTCL_NEXT_POS>();
  auto charge_ps = ptcls->get<PTCL_CHARGE>();
  auto first_ionizeZ_ps = ptcls->get<PTCL_FIRST_IONIZEZ>();
  auto prev_ionize_ps = ptcls->get<PTCL_PREV_IONIZE>();
  auto first_ionizeT_ps = ptcls->get<PTCL_FIRST_IONIZET>();
  auto psCapacity = ptcls->capacity();
  //delete later
  auto pos_prev=ptcls->get<0>();
  //kokkos random
  auto& rpool = gp.rand_pool; 
  auto lambda = PS_LAMBDA(const int &elem, const int &pid, const int &mask) {
    // invalid elem_ids init to -1
    if(mask > 0 && elm_ids[pid] >= 0) {
     // element of next_pos
      o::LO el = elm_ids[pid];
      auto ptcl = pid_ps(pid);
      auto ptcl_global=pid_ps_global(pid);
      auto pos = p::makeVector3(pid, new_pos);
      auto charge = charge_ps(pid);
      auto pos_previous = p::makeVector3(pid, pos_prev);
      if(false && debug>1) {
        printf(" IONIZE particle %d pid %d timestep %d positions %.15e, %.15e, %.15e\n", 
          ptcl, pid, iTimeStep, pos_previous[0], pos_previous[1], pos_previous[2]);
      }
      o::Real tlocal = 0;
      o::Real nlocal = 0;
      if(!use2DRatesData) {
        auto bcc = o::zero_vector<4>();
        p::findBCCoordsInTet(coords, mesh2verts, pos, el, bcc);
        tlocal = p::interpolateTetVtx(mesh2verts, tIonVtx, el, bcc, 1);
        nlocal = p::interpolateTetVtx(mesh2verts, densVtx, el, bcc, 1);
      }
      if(charge > maxCharge)
        charge = 0;
      // from data array
      if(use2DRatesData) {
        bool cylSymm = true;
        auto dens = p::interpolate2dField(densIon_d, x0Dens, z0Dens, dxDens, 
          dzDens, nxDens, nzDens, pos, cylSymm, 1,0,false);
        auto temp = p::interpolate2dField(temIon_d, x0Temp, z0Temp, dxTemp,
          dzTemp, nxTemp, nzTemp, pos, cylSymm, 1,0,false);
        
        if(debug>1)
          printf(" Ionization point: ptcl %d timestep %d position %g"
            " %g %g dens2D %g temp2D %g  tlocal %g nlocal %g  nxTemp %d nzTemp %d\n", 
            ptcl, iTimeStep, pos[0], pos[1], pos[2], dens, temp, tlocal, 
            nlocal, nxTemp, nzTemp);
        
        nlocal = dens;
        tlocal = temp;
      } 
      o::Real rateIon = interpolateRateCoeff(iRates, gridTemp, gridDens, tlocal, 
        nlocal, gridT0, gridD0, dTem, dDens, nTRates, nDRates, charge);

      o::Real P1 = 1.0 - exp(-dt/rateIon);
      double randn = 0;
      if(useGitrRnd) {
        randn = testGitrPtclStepData[ptcl_global*testGNT*testGDof + iTimeStep*testGDof + testGIind];
        if(debug>1)
          printf("gitrRnd:ioni ptcl %d  ptcl_global %ld t %d rand %g\n",
           ptcl, ptcl_global, iTimeStep, randn);
      } else if (useCudaRnd) {
        //NOTE : states for all particles to be initialized in all ranks
        auto localState = cuStates[ptcl_global];
        randn = curand_uniform(&localState);
        cuStates[ptcl_global] = localState;
        if(debug>1)
          printf("cudaRndNums-ioni ptcl %d ptcl_global %ld tstep %d rand %g\n",
           ptcl, ptcl_global, iTimeStep, randn);
      } else {
        //TODO use state index ? 
        auto rnd = rpool.get_state(); //rpool.get_state(pid)  ?
        randn = rnd.drand();
        rpool.free_state(rnd);
      }
      auto xfid = xfaces_d[pid];
      auto first_iz = first_ionizeZ_ps(pid);
      if(xfid < 0 && randn <= P1) {
          charge_ps(pid) = charge+1;
      }
      prev_ionize_ps(pid) = 1;
      if(o::are_close(first_iz, 0)) {
          first_ionizeZ_ps(pid) = pos[2]; // z
      } else if(o::are_close(first_iz, 0)) {
        auto fit = first_ionizeT_ps(pid);
        first_ionizeT_ps(pid) = fit + dt;
      } 
      if(debug>1)
        printf(" ionizable %d ptcl %d timestep %d charge %d randn %g P1 %g rateIon %g dt %gn",
          xfid<0, ptcl, iTimeStep, charge_ps(pid), randn, P1, rateIon, dt);
    } //mask 
  };
  p::parallel_for(ptcls,lambda, "ionizeKernel");
} 


inline void gitrm_recombine(PS* ptcls, const GitrmIonizeRecombine& gir, 
   GitrmParticles& gp, const GitrmMesh& gm, const o::LOs& elm_ids, int debug = 0) {
  if(debug) {
    MPI_Barrier(MPI_COMM_WORLD);
    printf("Recombination \n");
  }
  auto& mesh = gm.mesh;
  auto densIon_d = gm.getDensIon();
  auto temIon_d = gm.getTemIon();
  const auto x0Dens = gm.densIonX0;
  const auto z0Dens = gm.densIonZ0;
  const auto nxDens = gm.densIonNx;
  const auto nzDens = gm.densIonNz;
  const auto dxDens = gm.densIonDx;
  const auto dzDens = gm.densIonDz;
  const auto x0Temp = gm.tempIonX0;
  const auto z0Temp = gm.tempIonZ0;
  const auto nxTemp = gm.tempIonNx;
  const auto nzTemp = gm.tempIonNz;
  const auto dxTemp = gm.tempIonDx;
  const auto dzTemp = gm.tempIonDz;

  const bool useCudaRnd = gp.useCudaRnd;
  auto* cuStates =  gp.cudaRndStates;
  
  const int useGitrRnd = gp.useGitrRndNums;
  if(!gp.ranRecombination)
    gp.ranRecombination = true;
  //#ifdef useGitrRndNums
  const auto& testGitrPtclStepData = gp.testGitrPtclStepData;
  const auto testGDof = gp.testGitrStepDataDof;
  const auto testGrecInd = gp.testGitrDataRecRandInd;
  const auto testGNT = gp.testGitrStepDataNumTsteps;
  const auto iTimeStep = iTimePlusOne - 1;
  if(useGitrRnd)
    OMEGA_H_CHECK(gp.testGitrOptIoniRec);
  else
    OMEGA_H_CHECK(!gp.testGitrOptIoniRec);
  //#endif

  auto use2DRatesData = USE_2DREADIN_IONI_REC_RATES;
  auto& xfaces_d = gp.wallCollisionFaceIds;
  auto dt = gp.timeStep;
  auto gridT0 = gir.recombTempGridMin;
  auto gridD0 = gir.recombDensGridMin;
  auto dTem = gir.recombTempGridDT;
  auto dDens = gir.recombDensGridDn;
  auto nTRates = gir.recombTempGridN;
  auto nDRates = gir.recombDensGridN;
  const auto& rRates = gir.recombinationRates; 
  const auto& gridTemp = gir.gridTempRec;
  const auto& gridDens = gir.gridDensRec; 
  const auto coords = mesh.coords();
  const auto mesh2verts = mesh.ask_elem_verts();
  const auto tIonVtx = mesh.get_array<o::Real>(o::VERT, "IonTempVtx");
  const auto densVtx = mesh.get_array<o::Real>(o::VERT, "IonDensityVtx");
  //const auto& tIonVtx = gm.densElVtx_d;
  //const auto& densVtx = gm.tempElVtx_d;
  auto pid_ps_global = ptcls->get<PTCL_ID_GLOBAL>(); //NELY ADDED
  auto pid_ps = ptcls->get<PTCL_ID>();
  auto new_pos = ptcls->get<PTCL_NEXT_POS>();
  auto charge_ps = ptcls->get<PTCL_CHARGE>();
  auto first_ionizeZ_ps = ptcls->get<PTCL_FIRST_IONIZEZ>();
  auto prev_recomb_ps = ptcls->get<PTCL_PREV_RECOMBINE>();
  auto psCapacity = ptcls->capacity();
  auto& rpool = gp.rand_pool; 
  // is elm_ids[pid] >= 0 make sure ptcl not intersected bdry
  auto lambda = PS_LAMBDA(const int &elem, const int &pid, const int &mask) {
    if(mask > 0 && elm_ids[pid] >= 0) {
      auto el = elm_ids[pid];
      auto ptcl = pid_ps(pid);
      auto ptcl_global=pid_ps_global(pid);
      auto charge = charge_ps(pid);
      auto pos = p::makeVector3(pid, new_pos);
      o::Real rateRecomb = 0;
      o::Real P1 = 0;
      if(charge > 0) {
        o::Real tlocal = 0;
        o::Real nlocal = 0;
        if(!use2DRatesData) {
          auto bcc = o::zero_vector<4>();
          p::findBCCoordsInTet(coords, mesh2verts, pos, el, bcc);
          // from tags
          tlocal = p::interpolateTetVtx(mesh2verts, tIonVtx, el, bcc, 1);
          nlocal = p::interpolateTetVtx(mesh2verts, densVtx, el, bcc, 1);
        }
        // from data array
        if(use2DRatesData) {
          //cylindrical symmetry, height (z) is same.
          // projecting point to y=0 plane, since 2D data is on const-y plane.
          bool cylSymm = true;
          auto dens = p::interpolate2dField(densIon_d, x0Dens, z0Dens, dxDens, 
            dzDens, nxDens, nzDens, pos, cylSymm,1,0,false);
          auto temp = p::interpolate2dField(temIon_d, x0Temp, z0Temp, dxTemp,
            dzTemp, nxTemp, nzTemp, pos, cylSymm,1,0,false);
          
          if(debug>1)
            printf(" Recomb Dens: ptcl %d x0 %g z0 %g dx %g dz %g nx %d " 
            " nz %d \n", ptcl, x0Dens, z0Dens, dxDens, dzDens, nxDens, nzDens);
          if(debug>1)
            printf(" Recomb Temp: ptcl %d x0 %g z0 %g dx %g dz %g nx %d " 
            " nz %d \n", ptcl, x0Temp, z0Temp, dxTemp,dzTemp, nxTemp, nzTemp);  
          if(debug>1)
            printf(" Recomb point: ptcl %d temp2D %g dens2D %g t3D %g d3D %g pos %g %g %g \n", 
              ptcl, temp, dens, tlocal, nlocal, pos[0], pos[1], pos[2]);
          nlocal = dens;
          tlocal = temp;
        }
        // rate is from global data
        rateRecomb = interpolateRateCoeff(rRates, gridTemp, gridDens, tlocal,
         nlocal, gridT0, gridD0, dTem, dDens, nTRates, nDRates, charge);
        P1 = 1.0 - exp(-dt/rateRecomb);

        double randn = 0;
        double randGitr = 0;
        int gitrInd = -1;
        if(useGitrRnd) {
          randGitr = testGitrPtclStepData[ptcl_global*testGNT*testGDof + 
            iTimeStep*testGDof + testGrecInd];
          randn = randGitr;
          if(debug>1)
            printf("gitrRnd:recomb ptcl %d ptcl_global %ld t %d rand %g\n",
             ptcl, ptcl_global, iTimeStep, randn);
        } else if (useCudaRnd) {
          auto localState = cuStates[ptcl_global];
          randn = curand_uniform(&localState);
          cuStates[ptcl_global] = localState;
          if(debug>1)
            printf("cudaRndNums-recomb ptcl %d tstep %d rand %g\n", ptcl, iTimeStep, randn);
        } else { 
          auto rnd = rpool.get_state();
          randn = rnd.drand();
          rpool.free_state(rnd);
        }
        auto xfid = xfaces_d[pid];
        auto first_iz = first_ionizeZ_ps(pid);
        if(xfid < 0 && randn <= P1) {
          charge_ps(pid) = charge-1;
          prev_recomb_ps(pid) = 1;
        }

        if(useGitrRnd && debug)
          gitrInd = ptcl*testGNT*testGDof+iTimeStep*testGDof+testGrecInd;
        
        if(debug>1)
          printf(" recomb %d ptcl %d  tstep %d charge %d randn %g P1 %g rateRecomb %g @ %d\n", 
            xfid<0, ptcl, iTimeStep, charge_ps(pid), randn, P1, rateRecomb, gitrInd);
      } //charge >0
    } //mask 
  };
  p::parallel_for(ptcls, lambda, "RecombineKernel");
} 

#endif
