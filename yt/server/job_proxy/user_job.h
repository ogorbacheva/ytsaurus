﻿#pragma once

#include "public.h"
#include "job.h"

#include <yt/server/job_agent/public.h>

#include <yt/ytlib/scheduler/job.pb.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

IJobPtr CreateUserJob(
    IJobHost* host,
    const NScheduler::NProto::TUserJobSpec& userJobSpec,
    const NJobAgent::TJobId& jobId,
    std::unique_ptr<IUserJobIO> userJobIO);

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
