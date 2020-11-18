#pragma once

#include "public.h"

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NHydra {

////////////////////////////////////////////////////////////////////////////////

IChangelogStoreFactoryPtr CreateLocalChangelogStoreFactory(
    TFileChangelogStoreConfigPtr config,
    const TString& threadName,
    const NProfiling::TRegistry& profiler);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
