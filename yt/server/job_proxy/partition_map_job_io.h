#pragma once

#include "private.h"
#include "user_job_io.h"

#include <ytlib/scheduler/public.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////

TAutoPtr<TUserJobIO> CreatePartitionMapJobIO(
    NScheduler::TJobIOConfigPtr ioConfig,
    NMetaState::TMasterDiscoveryConfigPtr mastersConfig,
    const NScheduler::NProto::TJobSpec& jobSpec);

////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
