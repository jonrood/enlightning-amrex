#include "enlightning_derive.H"
#include "enlightning_parm.H"

using namespace amrex;

void enlightning_derpres (const Box& bx, FArrayBox& pfab, int dcomp, int /*ncomp*/,
                  const FArrayBox& rhoefab, const Geometry& /*geomdata*/,
                  Real /*time*/, const int* /*bcrec*/, int /*level*/)
{
    auto const rhoe = rhoefab.array();
    auto       p    = pfab.array();
    amrex::ParallelFor(bx,
    [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
    {
        p(i,j,k,dcomp) = (Parm::eos_gamma-1.)*rhoe(i,j,k);
    });
}

void enlightning_dervel (const Box& bx, FArrayBox& velfab, int dcomp, int /*ncomp*/,
                  const FArrayBox& datfab, const Geometry& /*geomdata*/,
                  Real /*time*/, const int* /*bcrec*/, int /*level*/)
{
    auto const dat = datfab.array();
    auto       vel = velfab.array();
    amrex::ParallelFor(bx,
    [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
    {
        vel(i,j,k,dcomp) = vel(i,j,k,1)/vel(i,j,k,0);
    });
}
