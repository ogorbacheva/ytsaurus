#pragma once

#include "public.h"

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/ytlib/security_client/public.h>

#include <yt/yt/core/ytree/public.h>

#include <yt/yt/core/logging/public.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

TJobId GenerateJobId(NObjectClient::TCellTag tag, NNodeTrackerClient::TNodeId nodeId);
TJobId JobIdFromAllocationId(TAllocationId allocationId);
NNodeTrackerClient::TNodeId NodeIdFromAllocationId(TAllocationId allocationId);

////////////////////////////////////////////////////////////////////////////////

NSecurityClient::TSerializableAccessControlList MakeOperationArtifactAcl(const NSecurityClient::TSerializableAccessControlList& acl);

////////////////////////////////////////////////////////////////////////////////

void ValidateInfinibandClusterName(const TString& name);

////////////////////////////////////////////////////////////////////////////////

// TODO(eshcherbin): Use for all testing delays.
//! Used for testing purposes.
void Delay(TDuration delay, EDelayType delayType = EDelayType::Async);
void MaybeDelay(const TDelayConfigPtr& delayConfig);

////////////////////////////////////////////////////////////////////////////////

EAllocationState JobStateToAllocationState(const EJobState jobState);

////////////////////////////////////////////////////////////////////////////////

struct TAllocationToAbort
{
    TAllocationId AllocationId;
    // TODO(pogorelov): Make AbortReason non-nullable.
    std::optional<NScheduler::EAbortReason> AbortReason;
};

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

void ToProto(NProto::NNode::TAllocationToAbort* protoAllocationToAbort, const TAllocationToAbort& allocationToAbort);

void FromProto(TAllocationToAbort* allocationToAbort, const NProto::NNode::TAllocationToAbort& protoAllocationToAbort);

} // namespace NProto

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
