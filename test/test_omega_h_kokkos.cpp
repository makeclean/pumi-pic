#include <fstream>
#include <iostream>
#include <cmath>
#include <utility>

#include <Omega_h_for.hpp>
#include <Omega_h_file.hpp>  //gmsh
#include <Omega_h_array.hpp>
#include <Omega_h_mesh.hpp>

/* 
   Uncommenting the below will compile
   Usually kokkos is included within omega_h
*/
//#include <Kokkos_Core.hpp>

int main(int argc, char** argv) {
  Omega_h::Library lib = Omega_h::Library(&argc, &argv);
/*
  Uncommenting the below will run
  Usually kokkos is initialized and finalized by Omega_h::Library
*/
  //Kokkos::initialize(argc, argv);

  Kokkos::View<int*> arr("test_array", 5);

  return 0;
}
