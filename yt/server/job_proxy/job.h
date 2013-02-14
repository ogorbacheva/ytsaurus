﻿#pragma once

#include "public.h"

#include <ytlib/chunk_client/public.h>
#include <ytlib/scheduler/job.pb.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

//! Represents a context for running jobs inside job proxy.
struct IJobHost
{
    virtual ~IJobHost()
    { }

    virtual TJobProxyConfigPtr GetConfig() = 0;
    virtual const NScheduler::NProto::TJobSpec& GetJobSpec() = 0;

    virtual NScheduler::NProto::TNodeResources GetResourceUsage() = 0;
    virtual void SetResourceUsage(const NScheduler::NProto::TNodeResources& usage) = 0;

    virtual void ReleaseNetwork() = 0;
};

////////////////////////////////////////////////////////////////////////////////

//! Represents a job running inside job proxy.
struct IJob
    : public virtual TRefCounted
{
    virtual ~IJob()
    { }

    virtual NScheduler::NProto::TJobResult Run() = 0;
    virtual std::vector<NChunkClient::TChunkId> GetFailedChunks() const = 0;
    virtual double GetProgress() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
