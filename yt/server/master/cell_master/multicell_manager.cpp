#include "multicell_manager.h"
#include "config.h"
#include "config_manager.h"
#include "bootstrap.h"
#include "private.h"
#include "automaton.h"
#include "serialize.h"
#include "hydra_facade.h"
#include "world_initializer.h"
#include "helpers.h"

#include <yt/core/ytree/ypath_client.h>

#include <yt/core/concurrency/periodic_executor.h>

#include <yt/core/rpc/retrying_channel.h>

#include <yt/client/object_client/helpers.h>

#include <yt/ytlib/hive/cell_directory.h>

#include <yt/server/lib/hive/hive_manager.h>
#include <yt/server/lib/hive/mailbox.h>
#include <yt/server/lib/hive/helpers.h>
#include <yt/server/lib/hive/proto/hive_manager.pb.h>

#include <yt/server/lib/hydra/mutation.h>

#include <yt/server/master/security_server/security_manager.h>
#include <yt/server/master/security_server/user.h>

#include <yt/server/master/chunk_server/chunk_manager.h>

#include <yt/server/master/cell_master/proto/multicell_manager.pb.h>

namespace NYT::NCellMaster {

using namespace NElection;
using namespace NRpc;
using namespace NYTree;
using namespace NConcurrency;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NHiveServer;
using namespace NHiveClient;
using namespace NHiveClient::NProto;
using namespace NHydra;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = CellMasterLogger;
static const auto RegisterRetryPeriod = TDuration::MilliSeconds(100);

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EPrimaryRegisterState,
    (None)
    (Registering)
    (Registered)
);

class TMulticellManager::TImpl
    : public TMasterAutomatonPart
{
public:
    TImpl(
        TMulticellManagerConfigPtr config,
        TBootstrap* bootstrap)
        : TMasterAutomatonPart(bootstrap, EAutomatonThreadQueue::MulticellManager)
        , Config_(config)
    {
        YT_VERIFY(Config_);

        TMasterAutomatonPart::RegisterMethod(BIND(&TImpl::HydraRegisterSecondaryMasterAtPrimary, Unretained(this)));
        TMasterAutomatonPart::RegisterMethod(BIND(&TImpl::HydraOnSecondaryMasterRegisteredAtPrimary, Unretained(this)));
        TMasterAutomatonPart::RegisterMethod(BIND(&TImpl::HydraRegisterSecondaryMasterAtSecondary, Unretained(this)));
        TMasterAutomatonPart::RegisterMethod(BIND(&TImpl::HydraStartSecondaryMasterRegistration, Unretained(this)));
        TMasterAutomatonPart::RegisterMethod(BIND(&TImpl::HydraSetCellStatistics, Unretained(this)));

        RegisterLoader(
            "MulticellManager.Values",
            BIND(&TImpl::LoadValues, Unretained(this)));

        RegisterSaver(
            ESyncSerializationPriority::Values,
            "MulticellManager.Values",
            BIND(&TImpl::SaveValues, Unretained(this)));
    }

    void Initialize()
    {
        const auto& configManager = Bootstrap_->GetConfigManager();
        configManager->SubscribeConfigChanged(BIND(&TImpl::OnDynamicConfigChanged, MakeWeak(this)));

        if (Bootstrap_->IsSecondaryMaster()) {
            // NB: This causes a cyclic reference but we don't care.
            const auto& hiveManager = Bootstrap_->GetHiveManager();
            hiveManager->SubscribeIncomingMessageUpstreamSync(BIND(&TImpl::OnIncomingMessageUpstreamSync, MakeStrong(this)));

            const auto& hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
            hydraManager->SubscribeUpstreamSync(BIND(&TImpl::OnHydraUpstreamSync, MakeStrong(this)));
        }
    }


    void PostToMaster(
        const TCrossCellMessage& message,
        TCellTag cellTag,
        bool reliable)
    {
        auto encapsulatedMessage = BuildHiveMessage(message);
        DoPostMessage(std::move(encapsulatedMessage), TCellTagList{cellTag}, reliable);
    }

    void PostToMasters(
        const TCrossCellMessage& message,
        const TCellTagList& cellTags,
        bool reliable)
    {
        if (cellTags.empty()) {
            return;
        }

        auto encapsulatedMessage = BuildHiveMessage(message);
        DoPostMessage(std::move(encapsulatedMessage), cellTags, reliable);
    }

    void PostToSecondaryMasters(
        const TCrossCellMessage& message,
        bool reliable)
    {
        YT_VERIFY(Bootstrap_->IsPrimaryMaster());
        if (Bootstrap_->IsMulticell()) {
            PostToMasters(message, GetRegisteredMasterCellTags(), reliable);
        }
    }


    bool IsLocalMasterCellRegistered()
    {
        if (Bootstrap_->IsPrimaryMaster()) {
            return true;
        }

        if (RegisterState_ == EPrimaryRegisterState::Registered) {
            return true;
        }

        return false;
    }

    bool IsRegisteredSecondaryMaster(TCellTag cellTag)
    {
        return FindMasterEntry(cellTag) != nullptr;
    }

    ECellRoles GetMasterCellRoles(TCellTag cellTag)
    {
        auto* entry = FindMasterEntry(cellTag);
        return entry ? entry->Roles : ECellRoles::None;
    }

    const TCellTagList& GetRegisteredMasterCellTags()
    {
        return RegisteredMasterCellTags_;
    }

    int GetRegisteredMasterCellIndex(TCellTag cellTag)
    {
        return GetMasterEntry(cellTag)->Index;
    }


    TCellTag PickSecondaryMasterCell(double bias)
    {
        // List candidates.
        SmallVector<std::pair<TCellTag, i64>, MaxSecondaryMasterCells> candidates;
        if (Bootstrap_->IsSecondaryMaster()) {
            candidates.emplace_back(
                Bootstrap_->GetCellTag(),
                Bootstrap_->GetChunkManager()->Chunks().size());
        }
        for (const auto& [cellTag, entry] : RegisteredMasterMap_) {
            if (cellTag != Bootstrap_->GetPrimaryCellTag()) {
                candidates.emplace_back(cellTag, entry.Statistics.chunk_count());
            }
        }

        // Sanity check.
        if (candidates.empty()) {
            return InvalidCellTag;
        }

        // Compute the average number of chunks.
        i64 totalChunkCount = 0;
        for (auto [cellTag, chunkCount] : candidates) {
            totalChunkCount += chunkCount;
        }
        i64 avgChunkCount = totalChunkCount / candidates.size();

        // Split the candidates into two subsets: less-that-avg and more-than-avg.
        SmallVector<TCellTag, MaxSecondaryMasterCells> loCandidates;
        SmallVector<TCellTag, MaxSecondaryMasterCells> hiCandidates;
        for (auto [cellTag, chunkCount] : candidates) {
            if (chunkCount < avgChunkCount) {
                loCandidates.push_back(cellTag);
            } else {
                hiCandidates.push_back(cellTag);
            }
        }

        // Sample candidates.
        // loCandidates have weight 2^8 + bias * 2^8.
        // hiCandidates have weight 2^8.
        ui64 scaledBias = static_cast<ui64>(bias * (1ULL << 8));
        ui64 weightPerLo = (1ULL << 8) + scaledBias;
        ui64 totalLoWeight = weightPerLo * loCandidates.size();
        ui64 weightPerHi = 1ULL << 8;
        ui64 totalHiWeight = weightPerHi * hiCandidates.size();
        ui64 totalTokens = totalLoWeight + totalHiWeight;
        auto* mutationContext = GetCurrentMutationContext();
        ui64 random = mutationContext->RandomGenerator().Generate<ui64>() % totalTokens;
        return random < totalLoWeight
            ? loCandidates[random / weightPerLo]
            : hiCandidates[(random - totalLoWeight) / weightPerHi];
    }

    NProto::TCellStatistics ComputeClusterStatistics()
    {
        auto result = GetLocalCellStatistics();
        for (const auto& pair : RegisteredMasterMap_) {
            const auto& entry = pair.second;
            result += entry.Statistics;
        }
        return result;
    }


    IChannelPtr GetMasterChannelOrThrow(TCellTag cellTag, EPeerKind peerKind)
    {
        VERIFY_THREAD_AFFINITY_ANY();

        auto channel = FindMasterChannel(cellTag, peerKind);
        if (!channel) {
            THROW_ERROR_EXCEPTION("Unknown cell tag %v",
                cellTag);
        }
        return channel;
    }

    IChannelPtr FindMasterChannel(TCellTag cellTag, EPeerKind peerKind)
    {
        VERIFY_THREAD_AFFINITY_ANY();

        auto key = std::make_tuple(cellTag, peerKind);

        {
            TReaderGuard guard(MasterChannelCacheLock_);
            auto it = MasterChannelCache_.find(key);
            if (it != MasterChannelCache_.end()) {
                return it->second;
            }
        }


        const auto& cellDirectory = Bootstrap_->GetCellDirectory();
        auto cellId = Bootstrap_->GetCellId(cellTag);
        auto channel = cellDirectory->FindChannel(cellId, peerKind);
        if (!channel) {
            return nullptr;
        }

        // XXX(babenko): is this needed during forwarding?
        auto isRetryableError = BIND([] (const TError& error) {
            return
                error.GetCode() == NSecurityClient::EErrorCode::RequestQueueSizeLimitExceeded ||
                IsRetriableError(error);
        });
        channel = CreateRetryingChannel(Config_->MasterConnection, channel, isRetryableError);
        channel = CreateDefaultTimeoutChannel(channel, Config_->MasterConnection->RpcTimeout);

        {
            // NB: Insertions are racy.
            TWriterGuard guard(MasterChannelCacheLock_);
            MasterChannelCache_.emplace(key, channel);
        }

        return channel;
    }

    TMailbox* FindPrimaryMasterMailbox()
    {
        return PrimaryMasterMailbox_;
    }


    DEFINE_SIGNAL(void(TCellTag), ValidateSecondaryMasterRegistration);
    DEFINE_SIGNAL(void(TCellTag), ReplicateKeysToSecondaryMaster);
    DEFINE_SIGNAL(void(TCellTag), ReplicateValuesToSecondaryMaster);

private:
    const TMulticellManagerConfigPtr Config_;

    struct TMasterEntry
    {
        int Index = -1;
        NProto::TCellStatistics Statistics;
        TMailbox* Mailbox = nullptr;
        ECellRoles Roles = ECellRoles::None;

        void Save(NCellMaster::TSaveContext& context) const
        {
            using NYT::Save;
            Save(context, Index);
            Save(context, Statistics);
            Save(context, Roles);
        }

        void Load(NCellMaster::TLoadContext& context)
        {
            using NYT::Load;
            Load(context, Index);
            Load(context, Statistics);
            // COMPAT(shakurov)
            if (context.GetVersion() >= EMasterReign::CellRoles) {
                Load(context, Roles);
            }
        }
    };

    // NB: Must ensure stable order.
    std::map<TCellTag, TMasterEntry> RegisteredMasterMap_;
    TCellTagList RegisteredMasterCellTags_;
    EPrimaryRegisterState RegisterState_ = EPrimaryRegisterState::None;

    TMailbox* PrimaryMasterMailbox_ = nullptr;

    TPeriodicExecutorPtr RegisterAtPrimaryMasterExecutor_;
    TPeriodicExecutorPtr CellStatisticsGossipExecutor_;

    //! Caches master channels returned by FindMasterChannel and GetMasterChannelOrThrow.
    NConcurrency::TReaderWriterSpinLock MasterChannelCacheLock_;
    THashMap<std::tuple<TCellTag, EPeerKind>, IChannelPtr> MasterChannelCache_;


    virtual void OnAfterSnapshotLoaded()
    {
        TMasterAutomatonPart::OnAfterSnapshotLoaded();

        RegisteredMasterCellTags_.resize(RegisteredMasterMap_.size());

        const auto& hiveManager  = Bootstrap_->GetHiveManager();
        for (auto& [cellTag, entry] : RegisteredMasterMap_) {
            ValidateCellTag(cellTag);

            RegisteredMasterCellTags_[entry.Index] = cellTag;

            auto cellId = Bootstrap_->GetCellId(cellTag);
            entry.Mailbox = hiveManager->GetMailbox(cellId);

            if (cellTag == Bootstrap_->GetPrimaryCellTag()) {
                PrimaryMasterMailbox_ = entry.Mailbox;
            }

            YT_LOG_INFO("Master cell registered (CellTag: %v, CellIndex: %v)",
                cellTag,
                entry.Index);
        }
    }

    virtual void Clear() override
    {
        TMasterAutomatonPart::Clear();

        RegisteredMasterMap_.clear();
        RegisteredMasterCellTags_.clear();
        RegisterState_ = EPrimaryRegisterState::None;
        PrimaryMasterMailbox_ = nullptr;
    }


    void LoadValues(TLoadContext& context)
    {
        using NYT::Load;
        Load(context, RegisteredMasterMap_);
        Load(context, RegisterState_);
    }

    void SaveValues(TSaveContext& context) const
    {
        using NYT::Save;

        Save(context, RegisteredMasterMap_);
        Save(context, RegisterState_);
    }


    virtual void OnLeaderActive() override
    {
        TMasterAutomatonPart::OnLeaderActive();

        if (Bootstrap_->IsSecondaryMaster()) {
            RegisterAtPrimaryMasterExecutor_ = New<TPeriodicExecutor>(
                Bootstrap_->GetHydraFacade()->GetEpochAutomatonInvoker(EAutomatonThreadQueue::Periodic),
                BIND(&TImpl::OnStartSecondaryMasterRegistration, MakeWeak(this)),
                RegisterRetryPeriod);
            RegisterAtPrimaryMasterExecutor_->Start();

            CellStatisticsGossipExecutor_ = New<TPeriodicExecutor>(
                Bootstrap_->GetHydraFacade()->GetEpochAutomatonInvoker(EAutomatonThreadQueue::Periodic),
                BIND(&TImpl::OnCellStatisticsGossip, MakeWeak(this)));
            CellStatisticsGossipExecutor_->Start();
        }

        OnDynamicConfigChanged();
    }

    virtual void OnStopLeading() override
    {
        TMasterAutomatonPart::OnStopLeading();

        if (RegisterAtPrimaryMasterExecutor_) {
            RegisterAtPrimaryMasterExecutor_->Stop();
            RegisterAtPrimaryMasterExecutor_.Reset();
        }

        if (CellStatisticsGossipExecutor_) {
            CellStatisticsGossipExecutor_->Stop();
            CellStatisticsGossipExecutor_.Reset();
        }

        ClearCaches();
    }

    virtual void OnStopFollowing() override
    {
        TMasterAutomatonPart::OnStopFollowing();

        ClearCaches();
    }


    void ClearCaches()
    {
        TWriterGuard guard(MasterChannelCacheLock_);
        MasterChannelCache_.clear();
    }


    void HydraRegisterSecondaryMasterAtPrimary(NProto::TReqRegisterSecondaryMasterAtPrimary* request)
    {
        YT_VERIFY(Bootstrap_->IsPrimaryMaster());

        auto cellTag = request->cell_tag();
        try {
            ValidateSecondaryCellTag(cellTag);

            if (FindMasterEntry(cellTag))  {
                THROW_ERROR_EXCEPTION("Attempted to re-register secondary master %v", cellTag);
            }

            ValidateSecondaryMasterRegistration_.Fire(cellTag);

            RegisterMasterEntry(cellTag);

            ReplicateKeysToSecondaryMaster_.Fire(cellTag);
            ReplicateValuesToSecondaryMaster_.Fire(cellTag);

            for (const auto& pair : RegisteredMasterMap_) {
                if (pair.first == cellTag) {
                    continue;
                }

                {
                    // Inform others about the new secondary.
                    NProto::TReqRegisterSecondaryMasterAtSecondary request;
                    request.set_cell_tag(cellTag);
                    PostToMaster(request, pair.first, true);
                }
                {
                    // Inform the new secondary about others.
                    NProto::TReqRegisterSecondaryMasterAtSecondary request;
                    request.set_cell_tag(pair.first);
                    PostToMaster(request, cellTag, true);
                }
            }

            NProto::TRspRegisterSecondaryMasterAtPrimary response;
            PostToMaster(response, cellTag, true);
        } catch (const std::exception& ex) {
            NProto::TRspRegisterSecondaryMasterAtPrimary response;
            ToProto(response.mutable_error(), TError(ex).Sanitize());
            PostToMaster(response, cellTag, true);
        }
    }

    void HydraOnSecondaryMasterRegisteredAtPrimary(NProto::TRspRegisterSecondaryMasterAtPrimary* response)
    {
        YT_VERIFY(Bootstrap_->IsSecondaryMaster());

        if (response->has_error()) {
            auto error = FromProto<TError>(response->error());
            YT_LOG_ERROR_UNLESS(IsRecovery(), error, "Error registering at primary master");
            RegisterState_ = EPrimaryRegisterState::None;
            return;
        }

        RegisterState_ = EPrimaryRegisterState::Registered;

        YT_LOG_INFO_UNLESS(IsRecovery(), "Successfully registered at primary master");
    }

    void HydraRegisterSecondaryMasterAtSecondary(NProto::TReqRegisterSecondaryMasterAtSecondary* request)
    {
        YT_VERIFY(Bootstrap_->IsSecondaryMaster());

        auto cellTag = request->cell_tag();
        try {
            ValidateSecondaryCellTag(cellTag);

            if (FindMasterEntry(cellTag))  {
                THROW_ERROR_EXCEPTION("Attempted to re-register secondary master %v", cellTag);
            }

            RegisterMasterEntry(cellTag);
        } catch (const std::exception& ex) {
            YT_LOG_FATAL(ex, "Error registering secondary master %v", cellTag);
        }
    }

    void HydraStartSecondaryMasterRegistration(NProto::TReqStartSecondaryMasterRegistration* /*request*/)
    {
        YT_VERIFY(Bootstrap_->IsSecondaryMaster());

        if (RegisterState_ != EPrimaryRegisterState::None) {
            return;
        }

        YT_LOG_INFO_UNLESS(IsRecovery(), "Registering at primary master");

        RegisterState_ = EPrimaryRegisterState::Registering;
        RegisterMasterEntry(Bootstrap_->GetPrimaryCellTag());

        NProto::TReqRegisterSecondaryMasterAtPrimary request;
        request.set_cell_tag(Bootstrap_->GetCellTag());
        PostToMaster(request, PrimaryMasterCellTag, true);
    }

    void HydraSetCellStatistics(NProto::TReqSetCellStatistics* request)
    {
        YT_VERIFY(Bootstrap_->IsPrimaryMaster());

        auto cellTag = request->cell_tag();
        YT_LOG_INFO_UNLESS(IsRecovery(), "Received cell statistics gossip message (CellTag: %v)",
            cellTag);

        auto* entry = GetMasterEntry(cellTag);
        entry->Statistics = request->statistics();
    }


    void ValidateSecondaryCellTag(TCellTag cellTag)
    {
        const auto& config = Bootstrap_->GetConfig();
        for (auto cellConfig : config->SecondaryMasters) {
            if (CellTagFromId(cellConfig->CellId) == cellTag)
                return;
        }
        THROW_ERROR_EXCEPTION("Unknown secondary master cell tag %v", cellTag);
    }

    void ValidateCellTag(TCellTag cellTag)
    {
        const auto& config = Bootstrap_->GetConfig();
        if (CellTagFromId(config->PrimaryMaster->CellId) == cellTag)
            return;
        for (auto cellConfig : config->SecondaryMasters) {
            if (CellTagFromId(cellConfig->CellId) == cellTag)
                return;
        }
        THROW_ERROR_EXCEPTION("Unknown master cell tag %v", cellTag);
    }


    void RegisterMasterEntry(TCellTag cellTag)
    {
        YT_VERIFY(RegisteredMasterMap_.size() == RegisteredMasterCellTags_.size());
        int index = static_cast<int>(RegisteredMasterMap_.size());
        RegisteredMasterCellTags_.push_back(cellTag);

        auto [it, inserted] = RegisteredMasterMap_.insert(std::make_pair(cellTag, TMasterEntry()));
        YT_VERIFY(inserted);

        auto& entry = it->second;
        entry.Index = index;
        entry.Roles = GetCellRoles(cellTag);

        auto cellId = Bootstrap_->GetCellId(cellTag);
        const auto& hiveManager = Bootstrap_->GetHiveManager();
        entry.Mailbox = hiveManager->GetOrCreateMailbox(cellId);

        if (cellTag == Bootstrap_->GetPrimaryCellTag()) {
            PrimaryMasterMailbox_ = entry.Mailbox;
        }

        YT_LOG_INFO_UNLESS(IsRecovery(), "Master cell registered (CellTag: %v, CellIndex: %v)",
            cellTag,
            index);
    }

    ECellRoles GetCellRoles(TCellTag cellTag)
    {
        auto defaultRoles = cellTag == Bootstrap_->GetPrimaryCellTag()
            ? (ECellRoles::CypressNodeHost | ECellRoles::TransactionCoordinator)
            : (ECellRoles::CypressNodeHost | ECellRoles::ChunkHost);
        return GetDynamicConfig()->CellRoles.Value(cellTag, defaultRoles);
    }

    TMasterEntry* FindMasterEntry(TCellTag cellTag)
    {
        auto it = RegisteredMasterMap_.find(cellTag);
        return it == RegisteredMasterMap_.end() ? nullptr : &it->second;
    }

    TMasterEntry* GetMasterEntry(TCellTag cellTag)
    {
        auto it = RegisteredMasterMap_.find(cellTag);
        YT_VERIFY(it != RegisteredMasterMap_.end());
        return &it->second;
    }

    TMailbox* FindMasterMailbox(TCellTag cellTag)
    {
        // Fast path.
        if (cellTag == PrimaryMasterCellTag) {
            return PrimaryMasterMailbox_;
        }

        const auto* entry = FindMasterEntry(cellTag);
        if (!entry) {
            return nullptr;
        }

        return entry->Mailbox;
    }


    void OnStartSecondaryMasterRegistration()
    {
        YT_VERIFY(Bootstrap_->IsSecondaryMaster());

        const auto& worldInitializer = Bootstrap_->GetWorldInitializer();
        if (!worldInitializer->IsInitialized()) {
            return;
        }

        if (RegisterState_ != EPrimaryRegisterState::None) {
            return;
        }

        NProto::TReqStartSecondaryMasterRegistration request;
        CreateMutation(Bootstrap_->GetHydraFacade()->GetHydraManager(), request)
            ->CommitAndLog(Logger);
    }

    void OnCellStatisticsGossip()
    {
        YT_VERIFY(Bootstrap_->IsSecondaryMaster());

        if (!IsLocalMasterCellRegistered()) {
            return;
        }

        YT_LOG_INFO("Sending cell statistics gossip message");

        NProto::TReqSetCellStatistics request;
        request.set_cell_tag(Bootstrap_->GetCellTag());
        *request.mutable_statistics() = GetLocalCellStatistics();
        PostToMaster(request, PrimaryMasterCellTag, false);
    }

    NProto::TCellStatistics GetLocalCellStatistics()
    {
        NProto::TCellStatistics result;
        const auto& chunkManager = Bootstrap_->GetChunkManager();
        result.set_chunk_count(chunkManager->Chunks().GetSize());
        result.set_lost_vital_chunk_count(chunkManager->LostVitalChunks().size());
        return result;
    }

    // XXX(babenko): tx cells
    TFuture<void> SyncWithPrimaryCell()
    {
        if (!IsLocalMasterCellRegistered()) {
            return VoidFuture;
        }
        const auto& hiveManager = Bootstrap_->GetHiveManager();
        return hiveManager->SyncWith(Bootstrap_->GetPrimaryCellId(), false);
    }

    TFuture<void> OnIncomingMessageUpstreamSync(TCellId srcCellId)
    {
        if (srcCellId == Bootstrap_->GetPrimaryCellId()) {
            return VoidFuture;
        }
        return SyncWithPrimaryCell();
    }

    TFuture<void> OnHydraUpstreamSync()
    {
        return SyncWithPrimaryCell();
    }


    TRefCountedEncapsulatedMessagePtr BuildHiveMessage(
        const TCrossCellMessage& crossCellMessage)
    {
        if (const auto* protoPtr = std::get_if<TCrossCellMessage::TProtoMessage>(&crossCellMessage.Payload)) {
            return NHiveServer::SerializeMessage(*protoPtr->Message);
        }

        NObjectServer::NProto::TReqExecute hydraRequest;
        TSharedRefArray parts;
        if (const auto* clientPtr = std::get_if<TCrossCellMessage::TClientMessage>(&crossCellMessage.Payload)) {
            parts = clientPtr->Request->Serialize();
        } else if (const auto* servicePtr = std::get_if<TCrossCellMessage::TServiceMessage>(&crossCellMessage.Payload)) {
            auto requestMessage = servicePtr->Context->GetRequestMessage();
            auto requestHeader = servicePtr->Context->RequestHeader();
            auto updatedYPath = FromObjectId(servicePtr->ObjectId) + GetRequestTargetYPath(requestHeader);
            SetRequestTargetYPath(&requestHeader, updatedYPath);
            parts = SetRequestHeader(requestMessage, requestHeader);
        } else {
            YT_ABORT();
        }

        for (const auto& part : parts) {
            hydraRequest.add_request_parts(part.Begin(), part.Size());
        }

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* user = securityManager->GetAuthenticatedUser();
        hydraRequest.set_user_name(user->GetName());

        return NHiveServer::SerializeMessage(hydraRequest);
    }

    void DoPostMessage(
        TRefCountedEncapsulatedMessagePtr message,
        const TCellTagList& cellTags,
        bool reliable)
    {
        TMailboxList mailboxes;
        for (auto cellTag : cellTags) {
            if (cellTag == PrimaryMasterCellTag) {
                cellTag = Bootstrap_->GetPrimaryCellTag();
            }
            auto* mailbox = FindMasterMailbox(cellTag);
            if (mailbox) {
                mailboxes.push_back(mailbox);
            }
        }

        const auto& hiveManager = Bootstrap_->GetHiveManager();
        hiveManager->PostMessage(mailboxes, std::move(message), reliable);
    }


    const TDynamicMulticellManagerConfigPtr& GetDynamicConfig()
    {
        return Bootstrap_->GetConfigManager()->GetConfig()->MulticellManager;
    }

    void OnDynamicConfigChanged()
    {
        if (CellStatisticsGossipExecutor_) {
            CellStatisticsGossipExecutor_->SetPeriod(GetDynamicConfig()->CellStatisticsGossipPeriod);
        }

        for (auto& [cellTag, entry] : RegisteredMasterMap_) {
            entry.Roles = GetCellRoles(cellTag);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TMulticellManager::TMulticellManager(
    TMulticellManagerConfigPtr config,
    TBootstrap* bootstrap)
    : Impl_(New<TImpl>(config, bootstrap))
{ }

TMulticellManager::~TMulticellManager() = default;

void TMulticellManager::Initialize()
{
    Impl_->Initialize();
}

void TMulticellManager::PostToMaster(
    const TCrossCellMessage& message,
    TCellTag cellTag,
    bool reliable)
{
    Impl_->PostToMaster(message, cellTag, reliable);
}

void TMulticellManager::PostToMasters(
    const TCrossCellMessage& message,
    const NObjectClient::TCellTagList& cellTags,
    bool reliable)
{
    Impl_->PostToMasters(message, cellTags, reliable);
}

void TMulticellManager::PostToSecondaryMasters(
    const TCrossCellMessage& message,
    bool reliable)
{
    Impl_->PostToSecondaryMasters(message, reliable);
}

bool TMulticellManager::IsLocalMasterCellRegistered()
{
    return Impl_->IsLocalMasterCellRegistered();
}

bool TMulticellManager::IsRegisteredMasterCell(TCellTag cellTag)
{
    return Impl_->IsRegisteredSecondaryMaster(cellTag);
}

ECellRoles TMulticellManager::GetMasterCellRoles(NObjectClient::TCellTag cellTag)
{
    return Impl_->GetMasterCellRoles(cellTag);
}

const TCellTagList& TMulticellManager::GetRegisteredMasterCellTags()
{
    return Impl_->GetRegisteredMasterCellTags();
}

int TMulticellManager::GetRegisteredMasterCellIndex(TCellTag cellTag)
{
    return Impl_->GetRegisteredMasterCellIndex(cellTag);
}

TCellTag TMulticellManager::PickSecondaryMasterCell(double bias)
{
    return Impl_->PickSecondaryMasterCell(bias);
}

NProto::TCellStatistics TMulticellManager::ComputeClusterStatistics()
{
    return Impl_->ComputeClusterStatistics();
}

IChannelPtr TMulticellManager::GetMasterChannelOrThrow(TCellTag cellTag, EPeerKind peerKind)
{
    return Impl_->GetMasterChannelOrThrow(cellTag, peerKind);
}

IChannelPtr TMulticellManager::FindMasterChannel(TCellTag cellTag, EPeerKind peerKind)
{
    return Impl_->FindMasterChannel(cellTag, peerKind);
}

TMailbox* TMulticellManager::FindPrimaryMasterMailbox()
{
    return Impl_->FindPrimaryMasterMailbox();
}

DELEGATE_SIGNAL(TMulticellManager, void(TCellTag), ValidateSecondaryMasterRegistration, *Impl_);
DELEGATE_SIGNAL(TMulticellManager, void(TCellTag), ReplicateKeysToSecondaryMaster, *Impl_);
DELEGATE_SIGNAL(TMulticellManager, void(TCellTag), ReplicateValuesToSecondaryMaster, *Impl_);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellMaster
