#pragma once

#include "distributed_hydra_manager.h"

namespace NYT::NHydra {

////////////////////////////////////////////////////////////////////////////////

struct IDryRunHydraManager
    : public IDistributedHydraManager
{
    //! Loads snapshot and sets automaton to the correct state.
    virtual void DryRunLoadSnapshot(const NHydra::ISnapshotReaderPtr& reader) = 0;

    //! Replays changelog.
    virtual void DryRunReplayChangelog(IChangelogPtr changelog) = 0;

    //! Builds snapshot and saves it.
    virtual void DryRunBuildSnapshot() = 0;

    //! Shuts down logger and exits.
    virtual void DryRunShutdown() = 0;
};

DEFINE_REFCOUNTED_TYPE(IDryRunHydraManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
