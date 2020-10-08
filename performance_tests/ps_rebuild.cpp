#include <particle_structs.hpp>
#include <ppTiming.hpp>
#include "perfTypes.hpp"
#include "../particle_structs/test/Distribute.h"

PS* createSCS(int num_elems, int num_ptcls, kkLidView ppe, kkGidView elm_gids, int C, int sigma, int V);
PS* createCSR(int num_elems, int num_ptcls, kkLidView ppe, kkGidView elm_gids);

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  MPI_Init(&argc, &argv);

  /* Check commandline arguments */
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <num elems> <num ptcls> <distribution> <%% ptcls move>\n",
            argv[0]);
  }

  fprintf(stderr, "Test Command: \n %s %s %s %s %s\n", argv[0], argv[1], argv[2],argv[3], argv[4]);

  /* Enable timing on every process */
  pumipic::SetTimingVerbosity(0);

  {
    /* Create initial distribution of particles */
    int num_elems = atoi(argv[1]);
    int num_ptcls = atoi(argv[2]);
    int strat = atoi(argv[3]);
    kkLidView ppe("ptcls_per_elem", num_elems);
    kkGidView element_gids("",0);
    int* ppe_host = new int[num_elems];
    std::vector<int>* ids = new std::vector<int>[num_elems];
    distribute_particles(num_elems, num_ptcls, strat, ppe_host, ids);
    pumipic::hostToDevice(ppe, ppe_host);
    delete [] ppe_host;
    delete [] ids;


    /* Create particle structure */
    ParticleStructures structures;
    structures.push_back(std::make_pair("Sell-32-ne",
                                        createSCS(num_elems, num_ptcls, ppe, element_gids,
                                                  32, num_elems, 1024)));
    structures.push_back(std::make_pair("Sell-16-ne",
                                        createSCS(num_elems, num_ptcls, ppe, element_gids,
                                                  16, num_elems, 1024)));
    structures.push_back(std::make_pair("Sell-32-1024",
                                        createSCS(num_elems, num_ptcls, ppe, element_gids,
                                                  32, 1024, 1024)));
    structures.push_back(std::make_pair("Sell-16-1024",
                                        createSCS(num_elems, num_ptcls, ppe, element_gids,
                                                  16, 1024, 1024)));
    structures.push_back(std::make_pair("Sell-32-1",
                                        createSCS(num_elems, num_ptcls, ppe, element_gids,
                                                  32, 1, 1024)));
    structures.push_back(std::make_pair("Sell-16-1",
                                        createSCS(num_elems, num_ptcls, ppe, element_gids,
                                                  16, 1, 1024)));
    structures.push_back(std::make_pair("CSR",
                                        createCSR(num_elems, num_ptcls, ppe, element_gids)));

    const int ITERS = 100;
    printf("Performing %d iterations of rebuild on each structure\n", ITERS);
    /* Perform rebuild on particle structures */
    double percentMoved = atof(argv[4]);
    for (int i = 0; i < structures.size(); ++i) {
      std::string name = structures[i].first;
      PS* ptcls = structures[i].second;
      printf("Beginning rebuild on structure %s\n", name.c_str());
      for (int i = 0; i < ITERS; ++i) {
        kkLidView new_elms("new elems", ptcls->capacity());
        redistribute_particles(ptcls, strat, percentMoved, new_elms);
        Kokkos::Timer rebuild_timer;
        ptcls->rebuild(new_elms);
        float rebuild_time = rebuild_timer.seconds();
        pumipic::RecordTime(name.c_str(), rebuild_time);
      }
    }

    for (size_t i = 0; i < structures.size(); ++i)
      delete structures[i].second;
    structures.clear();
  }

  pumipic::SummarizeTime();
  Kokkos::finalize();
  return 0;
}

PS* createSCS(int num_elems, int num_ptcls, kkLidView ppe, kkGidView elm_gids, int C, int sigma, int V) {
  Kokkos::TeamPolicy<ExeSpace> policy(4, C);
  pumipic::SCS_Input<PerfTypes> input(policy, sigma, V, num_elems, num_ptcls, ppe, elm_gids);
  return new pumipic::SellCSigma<PerfTypes, MemSpace>(input);
}
PS* createCSR(int num_elems, int num_ptcls, kkLidView ppe, kkGidView elm_gids) {
  Kokkos::TeamPolicy<ExeSpace> po(32,Kokkos::AUTO);
  return new pumipic::CSR<PerfTypes, MemSpace>(po, num_elems, num_ptcls, ppe, elm_gids);
}
