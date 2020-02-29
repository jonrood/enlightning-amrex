#ifndef ENLIGHTNING_DERIVE_H_
#define ENLIGHTNING_DERIVE_H_

#include <AMReX_FArrayBox.H>
#include <AMReX_Geometry.H>

void enlightning_derpres (const amrex::Box& bx, amrex::FArrayBox& derfab, int dcomp, int ncomp,
                  const amrex::FArrayBox& datafab, const amrex::Geometry& geomdata,
                  amrex::Real time, const int* bcrec, int level);

void enlightning_dervel (const amrex::Box& bx, amrex::FArrayBox& derfab, int dcomp, int ncomp,
                 const amrex::FArrayBox& datafab, const amrex::Geometry& geomdata,
                 amrex::Real time, const int* bcrec, int level);

#endif
