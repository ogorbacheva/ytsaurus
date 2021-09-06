#pragma once

#include "public.h"

#include <yt/yt/server/node/exec_node/public.h>

#include <yt/yt/server/lib/job_agent/job_report.h>

#include <yt/yt/ytlib/chunk_client/public.h>

#include <yt/yt/ytlib/job_tracker_client/proto/job.pb.h>

#include <yt/yt/ytlib/job_prober_client/job_shell_descriptor_cache.h>

#include <yt/yt_proto/yt/client/node_tracker_client/proto/node.pb.h>

#include <yt/yt/core/actions/signal.h>

#include <yt/yt/core/misc/error.h>

#include <yt/yt/core/misc/optional.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NJobAgent {

////////////////////////////////////////////////////////////////////////////////

/*
 * \note Thread affinity: Control (unless noted otherwise)
 */
struct IJob
    : public virtual TRefCounted
{
    DECLARE_INTERFACE_SIGNAL(void(
        const NNodeTrackerClient::NProto::TNodeResources& resourceDelta),
        ResourcesUpdated);

    DECLARE_INTERFACE_SIGNAL(void(), PortsReleased);

    DECLARE_INTERFACE_SIGNAL(void(), JobPrepared);

    DECLARE_INTERFACE_SIGNAL(void(), JobFinished);

    virtual void Start() = 0;

    virtual void Abort(const TError& error) = 0;
    virtual void Fail() = 0;

    virtual TJobId GetId() const = 0;
    virtual TOperationId GetOperationId() const = 0;

    virtual EJobType GetType() const = 0;

    virtual const NJobTrackerClient::NProto::TJobSpec& GetSpec() const = 0;

    virtual int GetPortCount() const = 0;

    virtual EJobState GetState() const = 0;

    virtual EJobPhase GetPhase() const = 0;

    virtual int GetSlotIndex() const = 0;

    virtual NNodeTrackerClient::NProto::TNodeResources GetResourceUsage() const = 0;
    virtual std::vector<int> GetPorts() const = 0;
    virtual void SetPorts(const std::vector<int>& ports) = 0;

    virtual void SetResourceUsage(const NNodeTrackerClient::NProto::TNodeResources& newUsage) = 0;

    virtual NJobTrackerClient::NProto::TJobResult GetResult() const = 0;
    virtual void SetResult(const NJobTrackerClient::NProto::TJobResult& result) = 0;

    virtual double GetProgress() const = 0;
    virtual void SetProgress(double value) = 0;

    virtual i64 GetStderrSize() const = 0;
    virtual void SetStderrSize(i64 value) = 0;

    virtual void SetStderr(const TString& value) = 0;
    virtual void SetFailContext(const TString& value) = 0;
    virtual void SetProfile(const TJobProfile& value) = 0;
    virtual void SetCoreInfos(NCoreDump::TCoreInfos value) = 0;

    virtual const TChunkCacheStatistics& GetChunkCacheStatistics() const = 0;

    virtual NYson::TYsonString GetStatistics() const = 0;
    virtual void SetStatistics(const NYson::TYsonString& statistics) = 0;

    virtual void OnJobProxySpawned() = 0;

    virtual void PrepareArtifact(
        const TString& artifactName,
        const TString& pipePath) = 0;

    virtual void OnArtifactPreparationFailed(
        const TString& artifactName,
        const TString& artifactPath,
        const TError& error) = 0;

    virtual void OnArtifactsPrepared() = 0;
    virtual void OnJobPrepared() = 0;

    virtual TInstant GetStartTime() const = 0;
    virtual NJobAgent::TTimeStatistics GetTimeStatistics() const = 0;

    virtual TInstant GetStatisticsLastSendTime() const = 0;
    virtual void ResetStatisticsLastSendTime() = 0;

    virtual std::vector<NChunkClient::TChunkId> DumpInputContext() = 0;
    virtual TString GetStderr() = 0;
    virtual std::optional<TString> GetFailContext() = 0;

    virtual void BuildOrchid(NYTree::TFluentMap fluent) const = 0;

    /*
     * \note Thread affinity: any
     */
    virtual NYson::TYsonString PollJobShell(
        const NJobProberClient::TJobShellDescriptor& jobShellDescriptor,
        const NYson::TYsonString& parameters) = 0;

    virtual bool GetStored() const = 0;
    virtual void SetStored(bool value) = 0;

    virtual void HandleJobReport(TNodeJobReport&& statistics) = 0;
    virtual void ReportSpec() = 0;
    virtual void ReportStderr() = 0;
    virtual void ReportFailContext() = 0;
    virtual void ReportProfile() = 0;

    virtual void Interrupt() = 0;
};

DEFINE_REFCOUNTED_TYPE(IJob)

using TMasterJobFactory = TCallback<IJobPtr(
    TJobId jobId,
    TOperationId operationId,
    const NNodeTrackerClient::NProto::TNodeResources& resourceLimits,
    NJobTrackerClient::NProto::TJobSpec&& jobSpec)>;

using TSchedulerJobFactory = TCallback<IJobPtr(
    TJobId jobid,
    TOperationId operationId,
    const NNodeTrackerClient::NProto::TNodeResources& resourceLimits,
    NJobTrackerClient::NProto::TJobSpec&& jobSpec,
    const NExecNode::TControllerAgentDescriptor& agentInfo)>;

////////////////////////////////////////////////////////////////////////////////

void FillJobStatus(NJobTrackerClient::NProto::TJobStatus* jobStatus, const IJobPtr& job);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobAgent
