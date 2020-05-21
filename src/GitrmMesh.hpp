#ifndef GITRM_MESH_HPP
#define GITRM_MESH_HPP

#include <vector>
#include <cfloat>
#include <set>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <netcdf>
#include "pumipic_adjacency.hpp"
#include "GitrmInputOutput.hpp"
#include "Omega_h_mesh.hpp"

namespace o = Omega_h;
namespace p = pumipic;

//presheath efield is always used. Since it is const, set CONSTANT_EBFIELDS.
// sheath efield is calcualted efield, it is always used. Skip calling
// gitrm_calculateE for neutrals.

// D3D 0.8 to 2.45 m radial

namespace gitrm {
  const double surfaceAndMaterialModelZ = 74;
}

//TODO put in config class
const bool CREATE_GITR_MESH = false;
const int USE_READIN_CSR_BDRYFACES = 1;
const int WRITE_OUT_BDRY_FACES_FILE = 0;
const int D2BDRY_MIN_SELECT = 10;
const int D2BDRY_MEM_FACTOR = 1; //per 8G memory
const bool WRITE_TEXT_D2BDRY_FACES = false;
const bool WRITE_BDRY_FACE_COORDS_NC = false;
const bool WRITE_MESH_FACE_COORDS_NC = false;
const o::LO D2BDRY_GRIDS_PER_TET = 15;// if csr bdry not re-used

const int USE_2DREADIN_IONI_REC_RATES = 1;
const int USE3D_BFIELD = 0;
const int USE2D_INPUTFIELDS = 1;

// in GITR only constant EField is used.
const int USE_CONSTANT_FLOW_VELOCITY=1;
const int USE_CONSTANT_BFIELD = 1; //used for pisces
const int USE_CYL_SYMMETRY = 1;
const int PISCESRUN  = 1;
const o::LO BACKGROUND_Z = 1;
const o::Real BIAS_POTENTIAL = 250.0;
const o::LO BIASED_SURFACE = 1;
const o::Real CONSTANT_EFIELD0 = 0;
const o::Real CONSTANT_EFIELD1 = 0;
const o::Real CONSTANT_EFIELD2 = 0;

const o::Real CONSTANT_BFIELD0 = 0;
const o::Real CONSTANT_BFIELD1 = 0;
const o::Real CONSTANT_BFIELD2 = -0.08;

const o::Real CONSTANT_FLOW_VELOCITY0 = 0;
const o::Real CONSTANT_FLOW_VELOCITY1 = 0;
const o::Real CONSTANT_FLOW_VELOCITY2 = -20000;
// 3 vtx, 1 bdry faceId & 1 bdry elId as Reals.
enum { BDRY_FACE_STORAGE_SIZE_PER_FACE = 1, BDRY_FACE_STORAGE_IDS=0 };
const int BDRY_STORAGE_SIZE_PER_FACE = 1;
// Elements face type
enum {INTERIOR=1, EXPOSED=2};
const int SKIP_MODEL_IDS_FROM_DIST2BDRY = 0; //set to 0


#define MESHDATA(mesh) \
  const auto nel = mesh.nelems(); \
  const auto coords = mesh.coords(); \
  const auto mesh2verts = mesh.ask_elem_verts(); \
  const auto dual_elems = mesh.ask_dual().ab2b; \
  const auto dual_faces = mesh.ask_dual().a2ab; \
  const auto face_verts = mesh.ask_verts_of(2); \
  const auto down_r2f = mesh.ask_down(3, 2).ab2b; \
  const auto down_r2fs = mesh.ask_down(3, 2).ab2b; \
  const auto side_is_exposed = o::mark_exposed_sides(&mesh);

class GitrmMesh {
public:
  GitrmMesh(o::Mesh& m);
  //TODO delete tags ?
  ~GitrmMesh(){};

  GitrmMesh(GitrmMesh const&) = delete;
  GitrmMesh& operator =(GitrmMesh const&) = delete;

  void setMyCommRank();
  int getCommRank() const { return myRank;}
  void createSurfaceGitrMesh();
  void printBdryFaceIds(bool printIds=true, o::LO minNums=0);
  void printBdryFacesCSR(bool printIds=true, o::LO minNums=0);
  void test_preProcessDistToBdry();

  o::Mesh& mesh;
  int numDetectorSurfaceFaces = 0;
  o::LO numNearBdryElems = 0;
  o::LO numAddedBdryFaces = 0;

  //GitrmMesh() = default;
  // static bool hasMesh;

  /** Distance to bdry search
  */
  void preProcessBdryFacesBfs();
  o::Write<o::LO> makeCsrPtrs(o::Write<o::LO>& data, int tot, int& sum);
  void preprocessStoreBdryFacesBfs(o::Write<o::LO>& numBdryFaceIdsInElems,
    o::Write<o::LO>& bdryFacesCsrW, int csrSize);

  void writeDist2BdryFacesData(const std::string outFileName="d2bdryFaces.nc",
    int nD2BdryTetSubDiv=0);
  o::LOs bdryFacesCsrBFS;
  o::LOs bdryFacePtrsBFS;

  /** Store closest boundary faces in each element in the domain.
   * onlyMaterialSheath skip others consistent with GITR sheath hash creation. 
   */
  void preprocessSelectBdryFacesFromAll(bool onlyMaterialSheath=true);

  o::LOs bdryFacePtrsSelected;
  o::LOs bdryFacesSelectedCsr;
  void writeBdryFacesDataText(int, std::string fileName="bdryFacesData.txt");
  void writeBdryFaceCoordsNcFile(int mode, std::string fileName="meshFaces.nc");

  int readDist2BdryFacesData(const std::string &);
  o::LOs bdryCsrReadInDataPtrs;
  o::LOs bdryCsrReadInData;

  /** create ordered ids starting 0 for given geometric bdry ids. 
   */
  void setOrderedBdryIds(const o::LOs& gFaceIds, int& nSurfaces,
    o::LOs& orderedIds, const o::LOs& gFaceValues, bool fill = false);

  void markBdryFacesOnGeomModelIds(const o::LOs& gFaces,
     o::Write<o::LO>& mark_d, o::LO newVal, bool init);
  
  template<typename T>
  void createBdryFaceClassArray(const o::LOs& gFaceIds, o::Write<T>& bdryArray,
     const o::Read<T>& gFaceValues, bool fill = false) {
    const auto side_is_exposed = o::mark_exposed_sides(&mesh);
    auto faceClassIds = mesh.get_array<o::ClassId>(2, "class_id");
    auto size = gFaceIds.size();
    o::parallel_for(mesh.nfaces(), OMEGA_H_LAMBDA(const o::LO& fid) {
      if(side_is_exposed[fid]) {
        for(auto id=0; id < size; ++id) {
          if(gFaceIds[id] == faceClassIds[fid]) {
            auto v = (fill) ? gFaceValues[id] : 1;
            bdryArray[fid] = v;
          }
        }
      }
    });
  }

  void setFaceId2BdryFaceIdMap();
  o::LOs bdryFaceOrderedIds;
  int nbdryFaces = 0;

  void setFaceId2SurfaceAndMaterialIdMap();
  int nSurfMaterialFaces = 0;
  o::LOs surfaceAndMaterialOrderedIds;
  int nDetectSurfaces = 0;
  o::LOs detectorSurfaceOrderedIds;

  void setFaceId2BdryFaceMaterialsZmap();
  o::LOs bdryFaceMaterialZs;

  void initBField(const std::string &f="bFile");
  void load3DFieldOnVtxFromFile(const std::string, const std::string &,
    Field3StructInput&, o::Reals&);
  bool addTagsAndLoadProfileData(const std::string &, const std::string &,
    const std::string &f="gradfile");
  bool initBoundaryFaces(bool init, bool debug=false);
  void loadScalarFieldOnBdryFacesFromFile(const std::string, const std::string &,
    Field3StructInput &, int debug=0);
  void load1DFieldOnVtxFromFile(const std::string, const std::string &,
    Field3StructInput &, o::Reals&, o::Reals&, int debug=0);
  int markDetectorSurfaces(bool render=false);
  void writeResultAsMeshTag(o::Write<o::LO>& data_d);
  void test_interpolateFields(bool debug=false);
  void printDensityTempProfile(double rmax=0.2, int gridsR=20,
    double zmax=0.5, int gridsZ=10);
  void compareInterpolate2d3d(const o::Reals& data3d, const o::Reals& data2d,
    double x0, double z0, double dx, double dz, int nx, int nz, bool debug=false);

  std::string profileNcFile = "profile.nc";

  // Used in boundary init and if 2D field is used for particles
  o::Real bGridX0 = 0;
  o::Real bGridZ0 = 0;
  o::Real bGridDx = 0;
  o::Real bGridDz = 0;
  o::LO bGridNx = 0;
  o::LO bGridNz = 0;
  o::Real eGridX0 = 0;
  o::Real eGridZ0 = 0;
  o::Real eGridDx = 0;
  o::Real eGridDz = 0;
  o::LO eGridNx = 0;
  o::LO eGridNz = 0;

  //D3D_major rad =1.6955m; https://github.com/SCOREC/Fusion_Public/blob/master/
  // samples/D-g096333.03337/g096333.03337#L1033
  // field2D center may not coincide with mesh center
  o::Reals Efield_2d;
  o::Reals Bfield_2d;
  o::Reals densIon_d;
  o::Reals densEl_d;
  o::Reals temIon_d;
  o::Reals temEl_d;

  //Added for gradient file
  o::Reals gradTi_d;
  //o::Reals gradTiT_d;
  //o::Reals gradTiZ_d;
  o::Reals gradTe_d;
  //o::Reals gradTeT_d;
  //o::Reals gradTeZ_d;
  //Till here

  o::Real densIonX0 = 0;
  o::Real densIonZ0 = 0;
  o::LO densIonNx = 0;
  o::LO densIonNz = 0;
  o::Real densIonDx = 0;
  o::Real densIonDz = 0;
  o::Real tempIonX0 = 0;
  o::Real tempIonZ0 = 0;
  o::LO tempIonNx = 0;
  o::LO tempIonNz = 0;
  o::Real tempIonDx = 0;
  o::Real tempIonDz = 0;

  o::Real densElX0 = 0;
  o::Real densElZ0 = 0;
  o::LO densElNx = 0;
  o::LO densElNz = 0;
  o::Real densElDx = 0;
  o::Real densElDz = 0;
  o::Real tempElX0 = 0;
  o::Real tempElZ0 = 0;
  o::LO tempElNx = 0;
  o::LO tempElNz = 0;
  o::Real tempElDx = 0;
  o::Real tempElDz = 0;

  //aDDED FOR GRADIENT FILE
  o::Real gradTiX0 = 0;
  o::Real gradTiZ0 = 0;
  o::Real gradTiNx = 0;
  o::Real gradTiNz = 0;
  o::Real gradTiDx = 0;
  o::Real gradTiDz = 0;

  o::Real gradTeX0 = 0;
  o::Real gradTeZ0 = 0;
  o::Real gradTeNx = 0;
  o::Real gradTeNz = 0;
  o::Real gradTeDx = 0;
  o::Real gradTeDz = 0;
  // till here
  // to replace tag
  o::Reals densIonVtx_d;
  o::Reals tempIonVtx_d;
  o::Reals densElVtx_d;
  o::Reals tempElVtx_d;
  //added for gradient file
  o::Reals gradTi_vtx_d;
  //o::Reals gradTiT_vtx_d;
  //o::Reals gradTiZ_vtx_d;
  o::Reals gradTe_vtx_d;
  //o::Reals gradTeT_vtx_d;
  //o::Reals gradTeZ_vtx_d;
  //till here
  //get model Ids by opening mesh/model in Simmodeler
  o::HostWrite<o::LO> detectorSurfaceModelIds;
  o::HostWrite<o::LO> bdryMaterialModelIds;
  o::HostWrite<o::LO> bdryMaterialModelIdsZ;
  o::HostWrite<o::LO> surfaceAndMaterialModelIds;
  o::Write<o::Real> larmorRadius_d;
  o::Write<o::Real> childLangmuirDist_d;
private:
  int myRank = -1;
  bool exists = false;
};
#endif// define