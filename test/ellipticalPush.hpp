#include <cmath>
#include <cstdio>
#include "pseudoXGCmTypes.hpp"

namespace ellipticalPush {
  double h; //x coordinate of center
  double k; //y coordinate of center
  double d; //ratio of ellipse minor axis length (a) to major axis length (b)

  void setup(SCS* scs, const double h_in, const double k_in,
      const double d_in) {
    printf("pushPosition, iter, pid, rad, x, y\n");
    h = h_in;
    k = k_in;
    d = d_in;
    auto x_nm1 = scs->get<0>();
    auto ptcl_id = scs->get<2>();
    auto ptcl_b = scs->get<3>();
    auto ptcl_phi = scs->get<4>();
    const auto h_d = h;
    const auto k_d = k;
    const auto d_d = d;
    auto setMajorAxis = SCS_LAMBDA(const int&, const int& pid, const int& mask) {
      if(mask) {
        const auto w = x_nm1(pid,0);
        const auto z = x_nm1(pid,1);
        const auto v = std::sqrt(std::pow(w-h_d,2) + std::pow(z-k_d,2));
        const auto phi = atan2(z-k_d,w-h_d);
        const auto b = std::sqrt(std::pow(v*cos(phi),2)/(d_d*d_d) +
                                std::pow(v*sin(phi),2));
        ptcl_phi(pid) = phi;
        ptcl_b(pid) = b;
        printf("pushPosition, %d, %d, %f, %f, %f\n", 0, ptcl_id(pid), phi, w, z);
      }
    };
    scs->parallel_for(setMajorAxis);
  }
  
  void push(SCS* scs, const double deg, const int iter) {
    Kokkos::Profiling::pushRegion("ellipticalPush");
    auto x_nm0 = scs->get<1>();
    auto ptcl_id = scs->get<2>();
    auto ptcl_b = scs->get<3>();
    auto ptcl_phi = scs->get<4>();
    const auto h_d = h;
    const auto k_d = k;
    const auto d_d = d;
    auto setPosition = SCS_LAMBDA(const int&, const int& pid, const int& mask) {
      if(mask) {
        const auto phi = ptcl_phi(pid);
        const auto b = ptcl_b(pid);
        const auto a = b*d_d;
        const auto rad = phi+deg*M_PI/180.0;
        const auto x = a*std::cos(rad)+h_d;
        const auto y = b*std::sin(rad)+k_d;
        x_nm0(pid,0) = x;
        x_nm0(pid,1) = y;
        ptcl_phi(pid) = rad;
        printf("pushPosition, %d, %d, %f, %f, %f\n", iter, ptcl_id(pid), rad, x, y);
      }
    };
    scs->parallel_for(setPosition);
    Kokkos::Profiling::popRegion();
  }
}