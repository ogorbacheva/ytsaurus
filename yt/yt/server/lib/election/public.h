#pragma once

#include <yt/yt/ytlib/election/public.h>

#include <yt/yt/core/misc/public.h>

namespace NYT::NElection {

////////////////////////////////////////////////////////////////////////////////

using TPeerIdSet = THashSet<TPeerId>;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(IElectionCallbacks)
DECLARE_REFCOUNTED_STRUCT(IElectionManager)

DECLARE_REFCOUNTED_STRUCT(TEpochContext)

DECLARE_REFCOUNTED_CLASS(TDistributedElectionManager)
DECLARE_REFCOUNTED_CLASS(TElectionManagerThunk)

DECLARE_REFCOUNTED_CLASS(TDistributedElectionManagerConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NElection
