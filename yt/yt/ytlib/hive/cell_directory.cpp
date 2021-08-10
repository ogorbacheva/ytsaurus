#include "cell_directory.h"
#include "config.h"
#include "private.h"

#include <yt/yt/ytlib/election/config.h>

#include <yt/yt/ytlib/hive/proto/cell_directory.pb.h>

#include <yt/yt/ytlib/hydra/config.h>
#include <yt/yt/ytlib/hydra/peer_channel.h>

#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt/core/concurrency/spinlock.h>

namespace NYT::NHiveClient {

using namespace NConcurrency;
using namespace NRpc;
using namespace NYTree;
using namespace NHydra;
using namespace NElection;
using namespace NNodeTrackerClient;
using namespace NRpc;

using NYT::ToProto;
using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

TCellPeerDescriptor::TCellPeerDescriptor()
    : Voting_(true)
{ }

TCellPeerDescriptor::TCellPeerDescriptor(const TNodeDescriptor& other, bool voting)
    : TNodeDescriptor(other)
    , Voting_(voting)
{ }

namespace {

TAddressMap ToAddressMap(const TCellPeerConfig& config, const TNetworkPreferenceList& networks)
{
    TAddressMap result;
    if (config.Address) {
        result.reserve(networks.size() + 1);
        for (const auto& network : networks) {
            YT_VERIFY(result.emplace(network, *config.Address).second);
        }
        // Default network must always be present in address map.
        result.emplace(DefaultNetworkName, *config.Address);
    }
    return result;
}

} // namespace

TCellPeerDescriptor::TCellPeerDescriptor(
    const TCellPeerConfig& config,
    const TNetworkPreferenceList& networks)
    : TNodeDescriptor(ToAddressMap(config, networks))
    , Voting_(config.Voting)
{ }

TCellPeerConfig TCellPeerDescriptor::ToConfig(const TNetworkPreferenceList& networks) const
{
    TCellPeerConfig config;
    config.Voting = Voting_;
    config.Address = IsNull() ? std::nullopt : std::make_optional(GetAddressOrThrow(networks));
    config.AlienCluster = AlienCluster_;
    return config;
}

////////////////////////////////////////////////////////////////////////////////

TCellDescriptor::TCellDescriptor(TCellId cellId)
    : CellId(cellId)
{ }

TCellConfigPtr TCellDescriptor::ToConfig(const TNetworkPreferenceList& networks) const
{
    auto config = New<TCellConfig>();
    config->CellId = CellId;
    config->Peers.reserve(Peers.size());
    for (const auto& peer : Peers) {
        config->Peers.emplace_back(peer.ToConfig(networks));
    }
    return config;
}

TCellInfo TCellDescriptor::ToInfo() const
{
    TCellInfo info;
    info.CellId = CellId;
    info.ConfigVersion = ConfigVersion;
    return info;
}

void ToProto(NProto::TCellPeerDescriptor* protoDescriptor, const TCellPeerDescriptor& descriptor)
{
    ToProto(protoDescriptor->mutable_node_descriptor(), descriptor);
    protoDescriptor->set_voting(descriptor.GetVoting());
    if (descriptor.GetAlienCluster()) {
        protoDescriptor->set_alien_cluster(*descriptor.GetAlienCluster());
    } else {
        protoDescriptor->clear_alien_cluster();
    }
}

void FromProto(TCellPeerDescriptor* descriptor, const NProto::TCellPeerDescriptor& protoDescriptor)
{
    FromProto(descriptor, protoDescriptor.node_descriptor());
    descriptor->SetVoting(protoDescriptor.voting());
    descriptor->SetAlienCluster(protoDescriptor.has_alien_cluster()
        ? std::make_optional(protoDescriptor.alien_cluster())
        : std::nullopt);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TCellInfo* protoInfo, const TCellInfo& info)
{
    ToProto(protoInfo->mutable_cell_id(), info.CellId);
    protoInfo->set_config_version(info.ConfigVersion);
}

void FromProto(TCellInfo* info, const NProto::TCellInfo& protoInfo)
{
    info->CellId = FromProto<TCellId>(protoInfo.cell_id());
    info->ConfigVersion = protoInfo.config_version();
}

void ToProto(NProto::TCellDescriptor* protoDescriptor, const TCellDescriptor& descriptor)
{
    ToProto(protoDescriptor->mutable_cell_id(), descriptor.CellId);
    protoDescriptor->set_config_version(descriptor.ConfigVersion);
    ToProto(protoDescriptor->mutable_peers(), descriptor.Peers);
}

void FromProto(TCellDescriptor* descriptor, const NProto::TCellDescriptor& protoDescriptor)
{
    descriptor->CellId = FromProto<TCellId>(protoDescriptor.cell_id());
    descriptor->ConfigVersion = protoDescriptor.config_version();
    descriptor->Peers = FromProto<std::vector<TCellPeerDescriptor>>(protoDescriptor.peers());
}

////////////////////////////////////////////////////////////////////////////////

class TCellDirectory::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TCellDirectoryConfigPtr config,
        IChannelFactoryPtr channelFactory,
        const TNetworkPreferenceList& networks,
        NLogging::TLogger logger)
        : Config_(std::move(config))
        , ChannelFactory_(std::move(channelFactory))
        , Networks_(networks)
        , Logger(std::move(logger))
    { }

    IChannelPtr FindChannel(TCellId cellId, EPeerKind peerKind)
    {
        auto guard = ReaderGuard(SpinLock_);
        auto it = RegisteredCellMap_.find(cellId);
        return it == RegisteredCellMap_.end() ? nullptr : it->second.Channels[peerKind];
    }

    IChannelPtr GetChannelOrThrow(TCellId cellId, EPeerKind peerKind)
    {
        auto channel = FindChannel(cellId, peerKind);
        if (!channel) {
            THROW_ERROR_EXCEPTION("Unknown cell %v",
                cellId);
        }
        return channel;
    }

    IChannelPtr GetChannel(TCellId cellId, EPeerKind peerKind)
    {
        auto channel = FindChannel(cellId, peerKind);
        YT_VERIFY(channel);
        return channel;
    }

    std::vector<TCellInfo> GetRegisteredCells()
    {
        auto guard = ReaderGuard(SpinLock_);
        std::vector<TCellInfo> result;
        result.reserve(RegisteredCellMap_.size());
        for (const auto& [cellId, entry] : RegisteredCellMap_) {
            result.push_back({cellId, entry.Descriptor.ConfigVersion});
        }
        return result;
    }

    bool IsCellUnregistered(TCellId cellId)
    {
        auto guard = ReaderGuard(SpinLock_);
        return UnregisteredCellIds_.find(cellId) != UnregisteredCellIds_.end();
    }

    bool IsCellRegistered(TCellId cellId)
    {
        auto guard = ReaderGuard(SpinLock_);
        return RegisteredCellMap_.find(cellId) != RegisteredCellMap_.end();
    }

    std::optional<TCellDescriptor> FindDescriptor(TCellId cellId)
    {
        auto guard = ReaderGuard(SpinLock_);
        auto it = RegisteredCellMap_.find(cellId);
        return it == RegisteredCellMap_.end() ? std::nullopt : std::make_optional(it->second.Descriptor);
    }

    TCellDescriptor GetDescriptorOrThrow(TCellId cellId)
    {
        auto result = FindDescriptor(cellId);
        if (!result) {
            THROW_ERROR_EXCEPTION("Unknown cell %v",
                cellId);
        }
        return *result;
    }

    std::optional<TString> FindPeerAddress(TCellId cellId, TPeerId peerId)
    {
        auto it = RegisteredCellMap_.find(cellId);
        if (!it) {
            return {};
        }

        const auto& peers = it->second.Descriptor.Peers;
        if (peerId >= std::ssize(peers)) {
            return {};
        }
        return peers[peerId].FindAddress(Networks_);
    }

    TSynchronizationResult Synchronize(const std::vector<TCellInfo>& knownCells)
    {
        auto guard = ReaderGuard(SpinLock_);

        TSynchronizationResult result;
        auto trySynchronize = [&] (bool trackMissingCells) {
            result = {};

            THashMap<TCellId, const TEntry*> missingMap;
            if (trackMissingCells) {
                for (const auto& [cellId, entry] : RegisteredCellMap_) {
                    YT_VERIFY(missingMap.emplace(cellId, &entry).second);
                }
            }

            for (const auto& knownCell : knownCells) {
                auto cellId = knownCell.CellId;
                if (auto it = RegisteredCellMap_.find(cellId)) {
                    if (trackMissingCells) {
                        YT_VERIFY(missingMap.erase(cellId) == 1);
                    }
                    const auto& entry = it->second;
                    if (knownCell.ConfigVersion < entry.Descriptor.ConfigVersion) {
                        result.ReconfigureRequests.push_back({entry.Descriptor, knownCell.ConfigVersion});
                    }
                } else {
                    if (!trackMissingCells) {
                        return false;
                    }
                    result.UnregisterRequests.push_back({cellId});
                }
            }

            for (auto [cellId, entry] : missingMap) {
                result.ReconfigureRequests.push_back({entry->Descriptor, -1});
            }

            return true;
        };

        if (!trySynchronize(knownCells.size() < RegisteredCellMap_.size())) {
            YT_VERIFY(trySynchronize(true));
        }

        return result;
    }

    bool ReconfigureCell(TCellConfigPtr config, int configVersion)
    {
        TCellDescriptor descriptor;
        descriptor.CellId = config->CellId;
        descriptor.ConfigVersion = configVersion;
        descriptor.Peers.reserve(config->Peers.size());
        for (const auto& peer : config->Peers) {
            descriptor.Peers.emplace_back(peer, Networks_);
        }
        return ReconfigureCell(descriptor);
    }

    bool ReconfigureCell(TPeerConnectionConfigPtr config, int configVersion)
    {
        auto cellConfig = New<TCellConfig>();
        cellConfig->CellId = config->CellId;
        if (config->Addresses) {
            for (const auto& address : *config->Addresses) {
                cellConfig->Peers.emplace_back(address);
            }
        }
        return ReconfigureCell(cellConfig, configVersion);
    }

    bool ReconfigureCell(const TCellDescriptor& descriptor)
    {
        auto guard = WriterGuard(SpinLock_);
        if (UnregisteredCellIds_.find(descriptor.CellId) != UnregisteredCellIds_.end()) {
            return false;
        }
        auto it = RegisteredCellMap_.find(descriptor.CellId);
        if (it == RegisteredCellMap_.end()) {
            it = RegisteredCellMap_.emplace(descriptor.CellId, TEntry(descriptor)).first;
            if (descriptor.ConfigVersion >= 0) {
                InitChannel(&it->second);
            }
            YT_LOG_DEBUG("Cell registered (CellId: %v, ConfigVersion: %v)",
                descriptor.CellId,
                descriptor.ConfigVersion);
            return true;
        } else if (it->second.Descriptor.ConfigVersion < descriptor.ConfigVersion) {
            it->second.Descriptor = descriptor;
            InitChannel(&it->second);
            YT_LOG_DEBUG("Cell reconfigured (CellId: %v, ConfigVersion: %v)",
                descriptor.CellId,
                descriptor.ConfigVersion);
            return true;
        }
        return false;
    }

    void RegisterCell(TCellId cellId)
    {
        TCellDescriptor descriptor;
        descriptor.CellId = cellId;
        ReconfigureCell(descriptor);
    }

    bool UnregisterCell(TCellId cellId)
    {
        auto guard = WriterGuard(SpinLock_);
        UnregisteredCellIds_.insert(cellId);
        if (RegisteredCellMap_.erase(cellId) == 0) {
            return false;
        }
        YT_LOG_INFO("Cell unregistered (CellId: %v)",
            cellId);
        return true;
    }

    void Clear()
    {
        auto guard = WriterGuard(SpinLock_);
        RegisteredCellMap_.clear();
    }

private:
    const TCellDirectoryConfigPtr Config_;
    const IChannelFactoryPtr ChannelFactory_;
    const TNetworkPreferenceList Networks_;
    const NLogging::TLogger Logger;

    struct TEntry
    {
        explicit TEntry(const TCellDescriptor& descriptor)
            : Descriptor(descriptor)
        { }

        TCellDescriptor Descriptor;
        TEnumIndexedVector<EPeerKind, IChannelPtr> Channels;
    };

    YT_DECLARE_SPINLOCK(TReaderWriterSpinLock, SpinLock_);
    THashMap<TCellId, TEntry> RegisteredCellMap_;
    THashSet<TCellId> UnregisteredCellIds_;


    void InitChannel(TEntry* entry)
    {
        auto peerConfig = New<TPeerConnectionConfig>();
        peerConfig->CellId = entry->Descriptor.CellId;
        peerConfig->Addresses.emplace();
        for (const auto& peer : entry->Descriptor.Peers) {
            if (!peer.IsNull()) {
                peerConfig->Addresses->push_back(peer.GetAddressOrThrow(Networks_));
            }
        }
        peerConfig->DiscoverTimeout = Config_->DiscoverTimeout;
        peerConfig->AcknowledgementTimeout = Config_->AcknowledgementTimeout;
        peerConfig->RediscoverPeriod = Config_->RediscoverPeriod;
        peerConfig->RediscoverSplay = Config_->RediscoverSplay;
        peerConfig->SoftBackoffTime = Config_->SoftBackoffTime;
        peerConfig->HardBackoffTime = Config_->HardBackoffTime;

        for (auto kind : TEnumTraits<EPeerKind>::GetDomainValues()) {
            entry->Channels[kind] = CreatePeerChannel(peerConfig, ChannelFactory_, kind);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TCellDirectory::TCellDirectory(
    TCellDirectoryConfigPtr config,
    IChannelFactoryPtr channelFactory,
    const TNetworkPreferenceList& networks,
    NLogging::TLogger logger)
    : Impl_(New<TImpl>(
        std::move(config),
        std::move(channelFactory),
        networks,
        std::move(logger)))
{ }

TCellDirectory::~TCellDirectory()
{ }

IChannelPtr TCellDirectory::FindChannel(TCellId cellId, EPeerKind peerKind)
{
    return Impl_->FindChannel(cellId, peerKind);
}

IChannelPtr TCellDirectory::GetChannelOrThrow(TCellId cellId, EPeerKind peerKind)
{
    return Impl_->GetChannelOrThrow(cellId, peerKind);
}

IChannelPtr TCellDirectory::GetChannel(TCellId cellId, EPeerKind peerKind)
{
    return Impl_->GetChannel(cellId, peerKind);
}

std::optional<TCellDescriptor> TCellDirectory::FindDescriptor(TCellId cellId)
{
    return Impl_->FindDescriptor(cellId);
}

TCellDescriptor TCellDirectory::GetDescriptorOrThrow(TCellId cellId)
{
    return Impl_->GetDescriptorOrThrow(cellId);
}

std::optional<TString> TCellDirectory::FindPeerAddress(TCellId cellId, TPeerId peerId)
{
    return Impl_->FindPeerAddress(cellId, peerId);
}

std::vector<TCellInfo> TCellDirectory::GetRegisteredCells()
{
    return Impl_->GetRegisteredCells();
}

bool TCellDirectory::IsCellUnregistered(TCellId cellId)
{
    return Impl_->IsCellUnregistered(cellId);
}

bool TCellDirectory::IsCellRegistered(TCellId cellId)
{
    return Impl_->IsCellRegistered(cellId);
}

TCellDirectory::TSynchronizationResult TCellDirectory::Synchronize(const std::vector<TCellInfo>& knownCells)
{
    return Impl_->Synchronize(knownCells);
}

bool TCellDirectory::ReconfigureCell(TCellConfigPtr config, int configVersion)
{
    return Impl_->ReconfigureCell(config, configVersion);
}

bool TCellDirectory::ReconfigureCell(TPeerConnectionConfigPtr config, int configVersion)
{
    return Impl_->ReconfigureCell(config, configVersion);
}

bool TCellDirectory::ReconfigureCell(const TCellDescriptor& descriptor)
{
    return Impl_->ReconfigureCell(descriptor);
}

void TCellDirectory::RegisterCell(TCellId cellId)
{
    Impl_->RegisterCell(cellId);
}

bool TCellDirectory::UnregisterCell(TCellId cellId)
{
    return Impl_->UnregisterCell(cellId);
}

void TCellDirectory::Clear()
{
    Impl_->Clear();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHiveClient
