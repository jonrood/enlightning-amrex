#include <enlightning.H>
#include <enlightning_K.H>
#include <enlightning_tagging.H>
#include <enlightning_parm.H>

#include <AMReX_MultiFabUtil.H>
#include <AMReX_ParmParse.H>

#include <climits>

using namespace amrex;

constexpr int enlightning::NUM_GROW;

BCRec     enlightning::phys_bc;

int       enlightning::verbose = 0;
IntVect   enlightning::hydro_tile_size {AMREX_D_DECL(1024,16,16)};
Real      enlightning::cfl       = 0.3;
int       enlightning::do_reflux = 1;
int       enlightning::refine_max_dengrad_lev   = -1;
Real      enlightning::refine_dengrad           = 1.0e10;

Real      enlightning::gravity = 0.0;

enlightning::enlightning ()
{}

enlightning::enlightning (Amr&            papa,
          int             lev,
          const Geometry& level_geom,
          const BoxArray& bl,
          const DistributionMapping& dm,
          Real            time)
    : AmrLevel(papa,lev,level_geom,bl,dm,time)
{
    if (do_reflux && level > 0) {
        flux_reg.reset(new FluxRegister(grids,dmap,crse_ratio,level,NUM_STATE));
    }

    buildMetrics();
}

enlightning::~enlightning ()
{}

void
enlightning::init (AmrLevel& old)
{
    auto& oldlev = dynamic_cast<enlightning&>(old);

    Real dt_new    = parent->dtLevel(level);
    Real cur_time  = oldlev.state[State_Type].curTime();
    Real prev_time = oldlev.state[State_Type].prevTime();
    Real dt_old    = cur_time - prev_time;
    setTimeLevel(cur_time,dt_old,dt_new);

    MultiFab& S_new = get_new_data(State_Type);
    FillPatch(old,S_new,0,cur_time,State_Type,0,NUM_STATE);
}

void
enlightning::init ()
{
    Real dt        = parent->dtLevel(level);
    Real cur_time  = getLevel(level-1).state[State_Type].curTime();
    Real prev_time = getLevel(level-1).state[State_Type].prevTime();
    Real dt_old = (cur_time - prev_time)/static_cast<Real>(parent->MaxRefRatio(level-1));
    setTimeLevel(cur_time,dt_old,dt);

    MultiFab& S_new = get_new_data(State_Type);
    FillCoarsePatch(S_new, 0, cur_time, State_Type, 0, NUM_STATE);
}

void
enlightning::initData ()
{
    BL_PROFILE("enlightning::initData()");

    const auto geomdata = geom.data();
    MultiFab& S_new = get_new_data(State_Type);

#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for (MFIter mfi(S_new); mfi.isValid(); ++mfi)
    {
        const Box& box = mfi.validbox();
        auto sfab = S_new.array(mfi);

        amrex::ParallelFor(box,
        [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
        {
            enlightning_initdata(i, j, k, sfab, geomdata);
        });
    }
}

void
enlightning::computeInitialDt (int                    finest_level,
                       int                    sub_cycle,
                       Vector<int>&           n_cycle,
                       const Vector<IntVect>& ref_ratio,
                       Vector<Real>&          dt_level,
                       Real                   stop_time)
{
    //
    // Grids have been constructed, compute dt for all levels.
    //
    if (level > 0) {
        return;
    }
    
    Real dt_0 = std::numeric_limits<Real>::max();
    int n_factor = 1;
    for (int i = 0; i <= finest_level; i++)
    {
        dt_level[i] = getLevel(i).initialTimeStep();
        n_factor   *= n_cycle[i];
        dt_0 = std::min(dt_0,n_factor*dt_level[i]);
    }
    
    //
    // Limit dt's by the value of stop_time.
    //
    const Real eps = 0.001*dt_0;
    Real cur_time  = state[State_Type].curTime();
    if (stop_time >= 0.0) {
        if ((cur_time + dt_0) > (stop_time - eps))
            dt_0 = stop_time - cur_time;
    }
    
    n_factor = 1;
    for (int i = 0; i <= finest_level; i++)
    {
        n_factor *= n_cycle[i];
        dt_level[i] = dt_0/n_factor;
    }
}

void
enlightning::computeNewDt (int                    finest_level,
                   int                    sub_cycle,
                   Vector<int>&           n_cycle,
                   const Vector<IntVect>& ref_ratio,
                   Vector<Real>&          dt_min,
                   Vector<Real>&          dt_level,
                   Real                   stop_time,
                   int                    post_regrid_flag)
{
    //
    // We are at the end of a coarse grid timecycle.
    // Compute the timesteps for the next iteration.
    //
    if (level > 0) {
        return;
    }

    for (int i = 0; i <= finest_level; i++)
    {
        dt_min[i] = getLevel(i).estTimeStep();
    }

    if (post_regrid_flag == 1) 
    {
	//
	// Limit dt's by pre-regrid dt
	//
	for (int i = 0; i <= finest_level; i++)
	{
	    dt_min[i] = std::min(dt_min[i],dt_level[i]);
	}
    }
    else 
    {
	//
	// Limit dt's by change_max * old dt
	//
	static Real change_max = 1.1;
	for (int i = 0; i <= finest_level; i++)
	{
	    dt_min[i] = std::min(dt_min[i],change_max*dt_level[i]);
	}
    }
    
    //
    // Find the minimum over all levels
    //
    Real dt_0 = std::numeric_limits<Real>::max();
    int n_factor = 1;
    for (int i = 0; i <= finest_level; i++)
    {
        n_factor *= n_cycle[i];
        dt_0 = std::min(dt_0,n_factor*dt_min[i]);
    }

    //
    // Limit dt's by the value of stop_time.
    //
    const Real eps = 0.001*dt_0;
    Real cur_time  = state[State_Type].curTime();
    if (stop_time >= 0.0) {
        if ((cur_time + dt_0) > (stop_time - eps)) {
            dt_0 = stop_time - cur_time;
        }
    }

    n_factor = 1;
    for (int i = 0; i <= finest_level; i++)
    {
        n_factor *= n_cycle[i];
        dt_level[i] = dt_0/n_factor;
    }
}

void
enlightning::post_regrid (int lbase, int new_finest)
{
}

void
enlightning::post_timestep (int iteration)
{
    BL_PROFILE("post_timestep");

    if (do_reflux && level < parent->finestLevel()) {
        MultiFab& S = get_new_data(State_Type);
        enlightning& fine_level = getLevel(level+1);
        fine_level.flux_reg->Reflux(S, 1.0, 0, 0, NUM_STATE, geom);
    }

    if (level < parent->finestLevel()) {
        avgDown();
    }
}

void
enlightning::postCoarseTimeStep (Real time)
{
    BL_PROFILE("postCoarseTimeStep()");

    // This only computes sum on level 0
    if (verbose >= 2) {
        printTotal();
    }
}

void
enlightning::printTotal () const
{
    const MultiFab& S_new = get_new_data(State_Type);
    std::array<Real,5> tot;
    for (int comp = 0; comp < 5; ++comp) {
        tot[comp] = S_new.sum(comp,true) * geom.ProbSize();
    }
#ifdef BL_LAZY
    Lazy::QueueReduction( [=] () mutable {
#endif
            ParallelDescriptor::ReduceRealSum(tot.data(), 5, ParallelDescriptor::IOProcessorNumber());
            amrex::Print().SetPrecision(17) << "\n[enlightning] Total mass       is " << tot[0] << "\n"
                                            <<   "      Total x-momentum is " << tot[1] << "\n"
                                            <<   "      Total y-momentum is " << tot[2] << "\n"
                                            <<   "      Total z-momentum is " << tot[3] << "\n"
                                            <<   "      Total energy     is " << tot[4] << "\n";
#ifdef BL_LAZY
        });
#endif
}

void
enlightning::post_init (Real)
{
    if (level > 0) return;
    for (int k = parent->finestLevel()-1; k >= 0; --k) {
        getLevel(k).avgDown();
    }

    if (verbose >= 2) {
        printTotal();
    }
}

void
enlightning::post_restart ()
{
}

void
enlightning::errorEst (TagBoxArray& tags, int, int, Real time, int, int)
{
    BL_PROFILE("enlightning::errorEst()");

    if (level < refine_max_dengrad_lev)
    {
        const MultiFab& S_new = get_new_data(State_Type);
        const Real cur_time = state[State_Type].curTime();
        MultiFab rho(S_new.boxArray(), S_new.DistributionMap(), 1, 1);
        FillPatch(*this, rho, rho.nGrow(), cur_time, State_Type, Density, 1, 0);

        const char   tagval = TagBox::SET;
//        const char clearval = TagBox::CLEAR;
        const Real dengrad_threshold = refine_dengrad;

#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for (MFIter mfi(rho,TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            const Box& bx = mfi.tilebox();

            const auto rhofab = rho.array(mfi);
            auto tag = tags.array(mfi);

            amrex::ParallelFor(bx,
            [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
            {
                enlightning_tag_denerror(i, j, k, tag, rhofab, dengrad_threshold, tagval);
            });
        }
    }
}

void
enlightning::read_params ()
{
    ParmParse pp("enlightning");

    pp.query("v", verbose);
 
    Vector<int> tilesize(AMREX_SPACEDIM);
    if (pp.queryarr("hydro_tile_size", tilesize, 0, AMREX_SPACEDIM))
    {
	for (int i=0; i<AMREX_SPACEDIM; i++) hydro_tile_size[i] = tilesize[i];
    }
   
    pp.query("cfl", cfl);

    Vector<int> lo_bc(AMREX_SPACEDIM), hi_bc(AMREX_SPACEDIM);
    pp.getarr("lo_bc", lo_bc, 0, AMREX_SPACEDIM);
    pp.getarr("hi_bc", hi_bc, 0, AMREX_SPACEDIM);
    for (int i = 0; i < AMREX_SPACEDIM; ++i) {
        phys_bc.setLo(i,lo_bc[i]);
        phys_bc.setHi(i,hi_bc[i]);
    }

    pp.query("do_reflux", do_reflux);

    pp.query("refine_max_dengrad_lev", refine_max_dengrad_lev);
    pp.query("refine_dengrad", refine_dengrad);

    pp.query("gravity", gravity);

    pp.query("eos_gamma", Parm::eos_gamma);

    Parm::Initialize();
}

void
enlightning::avgDown ()
{
    BL_PROFILE("enlightning::avgDown()");

    if (level == parent->finestLevel()) return;

    auto& fine_lev = getLevel(level+1);

    MultiFab& S_crse =          get_new_data(State_Type);
    MultiFab& S_fine = fine_lev.get_new_data(State_Type);

    amrex::average_down(S_fine, S_crse, fine_lev.geom, geom,
                        0, S_fine.nComp(), parent->refRatio(level));

    const int nghost = 0;
    computeTemp(S_crse, nghost);
}

void
enlightning::buildMetrics ()
{
    // make sure dx == dy == dz
    const Real* dx = geom.CellSize();
    if (std::abs(dx[0]-dx[1]) > 1.e-12*dx[0] || std::abs(dx[0]-dx[2]) > 1.e-12*dx[0]) {
        amrex::Abort("enlightning: must have dx == dy == dz\n");
    }
}

Real
enlightning::estTimeStep ()
{
    BL_PROFILE("enlightning::estTimeStep()");

    const auto dx = geom.CellSizeArray();
    const MultiFab& S = get_new_data(State_Type);

    Real estdt = amrex::ReduceMin(S, 0,
    [=] AMREX_GPU_HOST_DEVICE (Box const& bx, FArrayBox const& fab) noexcept -> Real
    {
        return enlightning_estdt(bx, fab, dx);
    });

    estdt *= cfl;
    ParallelDescriptor::ReduceRealMin(estdt);

    return estdt;
}

Real
enlightning::initialTimeStep ()
{
    return estTimeStep();
}

void
enlightning::computeTemp (MultiFab& State, int ng)
{
    BL_PROFILE("enlightning::computeTemp()");

    // This will reset Eint and compute Temperature 
#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for (MFIter mfi(State,TilingIfNotGPU()); mfi.isValid(); ++mfi)
    {
        const Box& bx = mfi.growntilebox(ng);
        auto const& sfab = State.array(mfi);

        amrex::ParallelFor(bx,
        [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
        {
            enlightning_compute_temperature(i,j,k,sfab);
        });
    }
}

