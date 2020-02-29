#include <AMReX_LevelBld.H>
#include <enlightning.H>

using namespace amrex;

class enlightningBld
    :
    public LevelBld
{
    virtual void variableSetUp () override;
    virtual void variableCleanUp () override;
    virtual AmrLevel *operator() () override;
    virtual AmrLevel *operator() (Amr&            papa,
                                  int             lev,
                                  const Geometry& level_geom,
                                  const BoxArray& ba,
                                  const DistributionMapping& dm,
                                  Real            time) override;
};

enlightningBld enlightning_bld;

LevelBld*
getLevelBld ()
{
    return &enlightning_bld;
}

void
enlightningBld::variableSetUp ()
{
    enlightning::variableSetUp();
}

void
enlightningBld::variableCleanUp ()
{
    enlightning::variableCleanUp();
}

AmrLevel*
enlightningBld::operator() ()
{
    return new enlightning;
}

AmrLevel*
enlightningBld::operator() (Amr&            papa,
                    int             lev,
                    const Geometry& level_geom,
                    const BoxArray& ba,
                    const DistributionMapping& dm,
                    Real            time)
{
    return new enlightning(papa, lev, level_geom, ba, dm, time);
}
