#pragma once

#include "public.h"

#include <yt/ytlib/chunk_client/chunk_replica.h>

#include <yt/ytlib/node_tracker_client/public.h>

#include <yt/core/concurrency/rw_spinlock.h>

#include <yt/core/misc/enum.h>
#include <yt/core/misc/nullable.h>
#include <yt/core/misc/property.h>

namespace NYT {
namespace NNodeTrackerClient {

////////////////////////////////////////////////////////////////////////////////

//! Network-related node information.
class TNodeDescriptor
{
public:
    TNodeDescriptor();
    explicit TNodeDescriptor(const Stroka& defaultAddress);
    explicit TNodeDescriptor(const TNullable<Stroka>& defaultAddress);
    explicit TNodeDescriptor(
        TAddressMap addresses,
        TNullable<Stroka> rack = Null,
        TNullable<Stroka> dc = Null);

    bool IsNull() const;

    const TAddressMap& Addresses() const;

    const Stroka& GetDefaultAddress() const;

    const Stroka& GetAddress(const TNetworkPreferenceList& networks) const;
    TNullable<Stroka> FindAddress(const TNetworkPreferenceList& networks) const;

    const TNullable<Stroka>& GetRack() const;
    const TNullable<Stroka>& GetDataCenter() const;

    void Persist(const TStreamPersistenceContext& context);

private:
    TAddressMap Addresses_;
    Stroka DefaultAddress_;
    TNullable<Stroka> Rack_;
    TNullable<Stroka> DataCenter_;

};

bool operator == (const TNodeDescriptor& lhs, const TNodeDescriptor& rhs);
bool operator != (const TNodeDescriptor& lhs, const TNodeDescriptor& rhs);

bool operator == (const TNodeDescriptor& lhs, const NProto::TNodeDescriptor& rhs);
bool operator != (const TNodeDescriptor& lhs, const NProto::TNodeDescriptor& rhs);

void FormatValue(TStringBuilder* builder, const TNodeDescriptor& descriptor, const TStringBuf& spec);
Stroka ToString(const TNodeDescriptor& descriptor);

// Accessors for some well-known addresses.
const Stroka& GetDefaultAddress(const TAddressMap& addresses);
const Stroka& GetDefaultAddress(const NProto::TAddressMap& addresses);

const Stroka& GetAddress(const TAddressMap& addresses, const TNetworkPreferenceList& networks);
TNullable<Stroka> FindAddress(const TAddressMap& addresses, const TNetworkPreferenceList& networks);

//! Please keep the items in this particular order: the further the better.
DEFINE_ENUM(EAddressLocality,
    (None)
    (SameDataCenter)
    (SameRack)
    (SameHost)
);

EAddressLocality ComputeAddressLocality(const TNodeDescriptor& first, const TNodeDescriptor& second);

namespace NProto {

void ToProto(NNodeTrackerClient::NProto::TAddressMap* protoAddresses, const NNodeTrackerClient::TAddressMap& addresses);
void FromProto(NNodeTrackerClient::TAddressMap* addresses, const NNodeTrackerClient::NProto::TAddressMap& protoAddresses);

void ToProto(NNodeTrackerClient::NProto::TNodeDescriptor* protoDescriptor, const NNodeTrackerClient::TNodeDescriptor& descriptor);
void FromProto(NNodeTrackerClient::TNodeDescriptor* descriptor, const NNodeTrackerClient::NProto::TNodeDescriptor& protoDescriptor);

} // namespace

////////////////////////////////////////////////////////////////////////////////

//! Caches node descriptors obtained by fetch requests.
/*!
 *  \note
 *  Thread affinity: thread-safe
 */
class TNodeDirectory
    : public TIntrinsicRefCounted
{
public:
    void MergeFrom(const NProto::TNodeDirectory& source);
    void MergeFrom(const TNodeDirectoryPtr& source);
    void DumpTo(NProto::TNodeDirectory* destination);

    void AddDescriptor(TNodeId id, const TNodeDescriptor& descriptor);

    const TNodeDescriptor* FindDescriptor(TNodeId id) const;
    const TNodeDescriptor& GetDescriptor(TNodeId id) const;
    const TNodeDescriptor& GetDescriptor(NChunkClient::TChunkReplica replica) const;
    std::vector<TNodeDescriptor> GetDescriptors(const NChunkClient::TChunkReplicaList& replicas) const;

    const TNodeDescriptor* FindDescriptor(const Stroka& address);
    const TNodeDescriptor& GetDescriptor(const Stroka& address);

    void Save(TStreamSaveContext& context) const;
    void Load(TStreamLoadContext& context);

private:
    NConcurrency::TReaderWriterSpinLock SpinLock_;
    yhash<TNodeId, const TNodeDescriptor*> IdToDescriptor_;
    yhash<Stroka, const TNodeDescriptor*> AddressToDescriptor_;
    std::vector<std::unique_ptr<TNodeDescriptor>> Descriptors_;

    void DoAddDescriptor(TNodeId id, const TNodeDescriptor& descriptor);
    void DoAddDescriptor(TNodeId id, const NProto::TNodeDescriptor& protoDescriptor);
    void DoAddCapturedDescriptor(TNodeId id, std::unique_ptr<TNodeDescriptor> descriptorHolder);

};

DEFINE_REFCOUNTED_TYPE(TNodeDirectory)

////////////////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerClient
} // namespace NYT
