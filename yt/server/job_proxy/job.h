﻿#pragma once

#include "public.h"

#include <ytlib/node_tracker_client/public.h>

#include <ytlib/chunk_client/public.h>

#include <core/rpc/public.h>

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
    virtual const NJobTrackerClient::NProto::TJobSpec& GetJobSpec() const = 0;

    virtual const NNodeTrackerClient::NProto::TNodeResources& GetResourceUsage() const = 0;
    virtual void SetResourceUsage(const NNodeTrackerClient::NProto::TNodeResources& usage) = 0;

    virtual void ReleaseNetwork() = 0;

    virtual NRpc::IChannelPtr GetMasterChannel() const = 0;

    virtual NChunkClient::IBlockCachePtr GetBlockCache() const = 0;

    virtual NNodeTrackerClient::TNodeDirectoryPtr GetNodeDirectory() const = 0;

};

////////////////////////////////////////////////////////////////////////////////

//! Represents a job running inside job proxy.
struct IJob
    : public virtual TRefCounted
{
    virtual ~IJob()
    { }

    virtual NJobTrackerClient::NProto::TJobResult Run() = 0;

    virtual std::vector<NChunkClient::TChunkId> GetFailedChunks() const = 0;

    virtual double GetProgress() const = 0;

    virtual NJobTrackerClient::NProto::TJobStatistics GetStatistics() const = 0;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
