#pragma once

#include "local_resource_allocator.h"

#include <yp/server/objects/public.h>

#include <yp/client/api/proto/data_model.pb.h>

namespace NYP::NClient::NApi::NProto {

////////////////////////////////////////////////////////////////////////////////

bool operator==(const TResourceStatus_TAllocation& lhs, const TResourceStatus_TAllocation& rhs);
bool operator!=(const TResourceStatus_TAllocation& lhs, const TResourceStatus_TAllocation& rhs);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NClient::NApi::NProto

namespace NYP::NServer::NScheduler {

////////////////////////////////////////////////////////////////////////////////

TResourceCapacities& operator += (TResourceCapacities& lhs, const TResourceCapacities& rhs);
TResourceCapacities operator + (const TResourceCapacities& lhs, const TResourceCapacities& rhs);
bool Dominates(const TResourceCapacities& lhs, const TResourceCapacities& rhs);
TResourceCapacities Max(const TResourceCapacities& a, const TResourceCapacities& b);
TResourceCapacities SubtractWithClamp(const TResourceCapacities& lhs, const TResourceCapacities& rhs);

bool IsHomogeneous(EResourceKind kind);

TResourceCapacities MakeCpuCapacities(ui64 capacity);
TResourceCapacities MakeMemoryCapacities(ui64 capacity);
TResourceCapacities MakeSlotCapacities(ui64 capacity);
TResourceCapacities MakeDiskCapacities(ui64 capacity, ui64 volumeSlots, ui64 bandwidth);
ui64 GetHomogeneousCapacity(const TResourceCapacities& capacities);
ui64 GetCpuCapacity(const TResourceCapacities& capacities);
ui64 GetMemoryCapacity(const TResourceCapacities& capacities);
ui64 GetSlotCapacity(const TResourceCapacities& capacities);
ui64 GetDiskCapacity(const TResourceCapacities& capacities);
ui64 GetDiskBandwidth(const TResourceCapacities& capacities);

TResourceCapacities GetResourceCapacities(const NClient::NApi::NProto::TResourceSpec& spec);
TResourceCapacities GetAllocationCapacities(const NClient::NApi::NProto::TResourceStatus_TAllocation& allocation);
bool GetAllocationExclusive(const NClient::NApi::NProto::TResourceStatus_TAllocation& allocation);
ui64 GetDiskVolumeRequestBandwidthGuarantee(const NClient::NApi::NProto::TPodSpec_TDiskVolumeRequest& request);
std::optional<ui64> GetDiskVolumeRequestOptionalBandwidthLimit(const NClient::NApi::NProto::TPodSpec_TDiskVolumeRequest& request);
TResourceCapacities GetDiskVolumeRequestCapacities(const NClient::NApi::NProto::TPodSpec_TDiskVolumeRequest& request);
bool GetDiskVolumeRequestExclusive(const NClient::NApi::NProto::TPodSpec_TDiskVolumeRequest& request);
NClient::NApi::NProto::EDiskVolumePolicy GetDiskVolumeRequestPolicy(const NClient::NApi::NProto::TPodSpec_TDiskVolumeRequest& request);

NClient::NApi::NProto::TResourceStatus_TAllocationStatistics ResourceCapacitiesToStatistics(
    const TResourceCapacities& capacities,
    EResourceKind kind);
TAllocationStatistics ComputeTotalAllocationStatistics(
    const std::vector<NYP::NClient::NApi::NProto::TResourceStatus_TAllocation>& scheduledAllocations,
    const std::vector<NYP::NClient::NApi::NProto::TResourceStatus_TAllocation>& actualAllocations);

TLocalResourceAllocator::TResource BuildAllocatorResource(
    const TObjectId& resourceId,
    const NClient::NApi::NProto::TResourceSpec& spec,
    const std::vector<NYP::NClient::NApi::NProto::TResourceStatus_TAllocation>& scheduledAllocations,
    const std::vector<NYP::NClient::NApi::NProto::TResourceStatus_TAllocation>& actualAllocations);

std::vector<TLocalResourceAllocator::TRequest> BuildAllocatorResourceRequests(
    const TObjectId& podId,
    const NObjects::NProto::TPodSpecEtc& spec,
    const NObjects::NProto::TPodStatusEtc& status,
    const std::vector<TLocalResourceAllocator::TResource>& resources);

void UpdatePodDiskVolumeAllocations(
    google::protobuf::RepeatedPtrField<NClient::NApi::NProto::TPodStatus_TDiskVolumeAllocation>* allocations,
    const std::vector<TLocalResourceAllocator::TRequest>& allocatorRequests,
    const std::vector<TLocalResourceAllocator::TResponse>& allocatorResponses);

void UpdateScheduledResourceAllocations(
    const TObjectId& podId,
    const TObjectId& podUuid,
    google::protobuf::RepeatedPtrField<NClient::NApi::NProto::TPodStatus_TResourceAllocation>* scheduledResourceAllocations,
    const std::vector<NObjects::TResource*>& nativeResources,
    const std::vector<TLocalResourceAllocator::TResource>& allocatorResources,
    const std::vector<TLocalResourceAllocator::TRequest>& allocatorRequests,
    const std::vector<TLocalResourceAllocator::TResponse>& allocatorResponses);

////////////////////////////////////////////////////////////////////////////////

struct TAllocationStatistics
{
    TResourceCapacities Capacities = {};
    bool Used = false;
    bool UsedExclusively = false;

    void Accumulate(const TLocalResourceAllocator::TAllocation& allocation);
    void Accumulate(const TLocalResourceAllocator::TRequest& request);
    void Accumulate(const NClient::NApi::NProto::TResourceStatus_TAllocation& allocation);
};

TAllocationStatistics Max(const TAllocationStatistics& lhs, const TAllocationStatistics& rhs);
TAllocationStatistics& operator+=(TAllocationStatistics& lhs, const TAllocationStatistics& rhs);
TAllocationStatistics operator+(const TAllocationStatistics& lhs, const TAllocationStatistics& rhs);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NScheduler
