#include "config.h"
#include "private.h"
#include "public.h"
#include "tablet_balancer.h"
#include "tablet_manager.h"

#include <yt/server/cell_master/bootstrap.h>
#include <yt/server/cell_master/hydra_facade.h>
#include <yt/server/cell_master/public.h>
#include <yt/server/cell_master/world_initializer.h>

#include <yt/server/tablet_server/tablet_manager.pb.h>

#include <yt/core/misc/numeric_helpers.h>

#include <queue>

namespace NYT {
namespace NTabletServer {

using namespace NConcurrency;
using namespace NTableClient;
using namespace NCypressClient;
using namespace NTabletNode;
using namespace NTabletServer::NProto;
using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TabletServerLogger;

////////////////////////////////////////////////////////////////////////////////

// Formatter for TMemoryUsage.
template <class T, class U>
Stroka ToString(const std::pair<T, U>& pair)
{
    return Format("(%v, %v)", pair.first, pair.second);
}

////////////////////////////////////////////////////////////////////////////////

class TTabletBalancer::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TTabletBalancerConfigPtr config,
        NCellMaster::TBootstrap* bootstrap)
        : Config_(std::move(config))
        , Bootstrap_(bootstrap)
        , BalanceExecutor_(New<TPeriodicExecutor>(
            Bootstrap_->GetHydraFacade()->GetAutomatonInvoker(),
            BIND(&TImpl::Balance, MakeWeak(this)),
            Config_->BalancePeriod))
        , EnabledCheckExecutor_(New<TPeriodicExecutor>(
            Bootstrap_->GetHydraFacade()->GetEpochAutomatonInvoker(),
            BIND(&TImpl::OnCheckEnabled, MakeWeak(this)),
            Config_->EnabledCheckPeriod))
    { }

    void Start()
    {
        BalanceExecutor_->Start();
        EnabledCheckExecutor_->Start();
    }

    void Stop()
    {
        EnabledCheckExecutor_->Stop();
        BalanceExecutor_->Stop();
    }

    void OnTabletHeartbeat(TTablet* tablet)
    {
        if (!Enabled_) {
            return;
        }

        if (!Config_->EnableTabletSizeBalancer) {
            return;
        }

        if (!IsObjectAlive(tablet) ||
            tablet->GetAction() ||
            QueuedTabletIds_.find(tablet->GetId()) != QueuedTabletIds_.end() ||
            !tablet->Replicas().empty())
        {
            return;
        }

        const auto& tabletManager = Bootstrap_->GetTabletManager();
        auto statistics = tabletManager->GetTabletStatistics(tablet);
        bool needAction = false;

        switch (tablet->GetInMemoryMode()) {
            case EInMemoryMode::None:
                if (statistics.UncompressedDataSize < Config_->MinTabletSize ||
                    statistics.UncompressedDataSize > Config_->MaxTabletSize)
                {
                    needAction = true;
                }
                break;

            case EInMemoryMode::Compressed:
            case EInMemoryMode::Uncompressed:
                if (statistics.MemorySize < Config_->MinInMemoryTabletSize ||
                    statistics.MemorySize > Config_->MaxInMemoryTabletSize)
                {
                    needAction = true;
                }
                break;

            default:
                Y_UNREACHABLE();
        }

        if (needAction) {
            TabletIdQueue_.push_back(tablet->GetId());
            QueuedTabletIds_.insert(tablet->GetId());
            LOG_DEBUG("Put tablet %v into balancer queue", tablet->GetId());
        }
    }

private:
    const TTabletBalancerConfigPtr Config_;
    const NCellMaster::TBootstrap* Bootstrap_;
    const NConcurrency::TPeriodicExecutorPtr BalanceExecutor_;
    const NConcurrency::TPeriodicExecutorPtr EnabledCheckExecutor_;

    bool Enabled_ = false;
    std::deque<TTabletId> TabletIdQueue_;
    yhash_set<TTabletId> QueuedTabletIds_;


    void Balance()
    {
        if (!Enabled_) {
            return;
        }

        if (TabletIdQueue_.empty()) {
            BalanceTabletCells();
        } else {
            BalanceTablets();
        }
    }

    bool CheckActiveTabletActions()
    {
        const auto& tabletManager = Bootstrap_->GetTabletManager();
        for (const auto& pair : tabletManager->TabletActions()) {
            const auto* action = pair.second;

            if (action->GetState() != ETabletActionState::Completed &&
                action->GetState() != ETabletActionState::Failed)
            {
                return true;
            }
        }

        return false;
    }

    void BalanceTabletCells()
    {
        const auto& tabletManager = Bootstrap_->GetTabletManager();
        const auto& cells = tabletManager->TabletCells();

        if (CheckActiveTabletActions()) {
            return;
        }

        if (cells.size() < 2) {
            return;
        }

        ReassignInMemoryTablets();
        // TODO(savrus) balance other tablets.
    }

    void ReassignInMemoryTablets()
    {
        if (!Config_->EnableInMemoryBalancer) {
            return;
        }

        const auto& tabletManager = Bootstrap_->GetTabletManager();
        const auto& cells = tabletManager->TabletCells();

        using TMemoryUsage = std::pair<i64, const TTabletCell*>;
        std::vector<TMemoryUsage> memoryUsage;
        i64 total = 0;
        memoryUsage.reserve(cells.size());
        for (const auto& pair : cells) {
            const auto* cell = pair.second;
            i64 size = cell->TotalStatistics().MemorySize;
            total += size;
            memoryUsage.emplace_back(size, cell);
        }

        std::sort(memoryUsage.begin(), memoryUsage.end());
        i64 mean = total / cells.size();
        std::priority_queue<TMemoryUsage, std::vector<TMemoryUsage>, std::greater<TMemoryUsage>> queue;

        for (const auto& pair : memoryUsage) {
            if (pair.first >= mean) {
                break;
            }
            queue.push(pair);
        }

        for (int index = memoryUsage.size() - 1; index >= 0; --index) {
            auto cellSize = memoryUsage[index].first;
            auto* cell = memoryUsage[index].second;

            for (const auto* tablet : cell->Tablets()) {
                if (tablet->GetInMemoryMode() == EInMemoryMode::None) {
                    continue;
                }

                if (queue.empty() || cellSize <= mean) {
                    break;
                }

                auto top = queue.top();

                if (static_cast<double>(cellSize - top.first) / cellSize < Config_->CellBalanceFactor) {
                    break;
                }

                auto statistics = tabletManager->GetTabletStatistics(tablet);
                auto tabletSize = statistics.MemorySize;

                if (tabletSize == 0) {
                    continue;
                }

                if (tabletSize < cellSize - top.first) {
                    LOG_DEBUG("Tablet balancer would like to move tablet (TabletId: %v, SrcCellId: %v, DstCellId: %v)",
                        tablet->GetId(),
                        cell->GetId(),
                        top.second->GetId());

                    queue.pop();
                    top.first += tabletSize;
                    cellSize -= tabletSize;
                    if (top.first < mean) {
                        queue.push(top);
                    }

                    TReqCreateTabletAction request;
                    request.set_kind(static_cast<int>(ETabletActionKind::Move));
                    ToProto(request.mutable_tablet_ids(), std::vector<TTabletId>{tablet->GetId()});
                    ToProto(request.mutable_cell_ids(), std::vector<TTabletCellId>{top.second->GetId()});

                    const auto& hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
                    CreateMutation(hydraManager, request)
                        ->CommitAndLog(Logger);
                }
            }
        }
    }


    void BalanceTablets()
    {
        // TODO(savrus) limit duration of single execution.
        const auto& tabletManager = Bootstrap_->GetTabletManager();

        while (!TabletIdQueue_.empty()) {
            auto tabletId = TabletIdQueue_.front();
            TabletIdQueue_.pop_front();
            QueuedTabletIds_.erase(tabletId);

            auto* tablet = tabletManager->FindTablet(tabletId);
            if (!tablet || !IsObjectAlive(tablet) || !tablet->Replicas().empty()) {
                continue;
            }

            auto statistics = tabletManager->GetTabletStatistics(tablet);
            bool needSplit = false;
            bool needMerge = false;

            switch (tablet->GetInMemoryMode()) {
                case EInMemoryMode::None:
                    if (statistics.UncompressedDataSize < Config_->MinTabletSize) {
                        needMerge = true;
                    } else if (statistics.UncompressedDataSize > Config_->MaxTabletSize) {
                        needSplit = true;
                    }
                    break;

                case EInMemoryMode::Compressed:
                case EInMemoryMode::Uncompressed:
                    if (statistics.MemorySize < Config_->MinInMemoryTabletSize) {
                        needMerge = true;
                    } else if (statistics.MemorySize > Config_->MaxInMemoryTabletSize) {
                        needSplit = true;
                    }
                    break;

                default:
                    Y_UNREACHABLE();
            }

            if (needMerge) {
                MergeTablet(tablet);
            } else if (needSplit) {
                SplitTablet(tablet);
            }
        }
    }

    i64 GetTabletSize(TTablet* tablet)
    {
        const auto& tabletManager = Bootstrap_->GetTabletManager();
        auto statistics = tabletManager->GetTabletStatistics(tablet);
        return tablet->GetInMemoryMode() == EInMemoryMode::None
            ? statistics.UncompressedDataSize
            : statistics.MemorySize;
    }

    void MergeTablet(TTablet* tablet)
    {
        auto* table = tablet->GetTable();

        if (table->Tablets().size() == 1) {
            return;
        }

        i64 minSize = tablet->GetInMemoryMode() == EInMemoryMode::None
            ? Config_->MinTabletSize
            : Config_->MinInMemoryTabletSize;
        i64 desiredSize = tablet->GetInMemoryMode() == EInMemoryMode::None
            ? Config_->DesiredTabletSize
            : Config_->DesiredInMemoryTabletSize;

        i64 size = GetTabletSize(tablet);

        int startIndex = tablet->GetIndex();
        int endIndex = tablet->GetIndex();

        while (size < minSize && startIndex > 0) {
            --startIndex;
            size += GetTabletSize(table->Tablets()[startIndex]);
        }
        while (size < minSize && endIndex < table->Tablets().size() - 1) {
            ++endIndex;
            size += GetTabletSize(table->Tablets()[endIndex]);
        }

        int newTabletCount = size == 0 ? 1 : DivCeil(size, desiredSize);

        std::vector<TTabletId> tabletIds;
        for (int index = startIndex; index <= endIndex; ++index) {
            tabletIds.push_back(table->Tablets()[index]->GetId());
        }

        LOG_DEBUG("Tablet balancer would like to reshard tablets (TabletIds: %v, NewTabletCount: %v)",
            tabletIds,
            newTabletCount);

        TReqCreateTabletAction request;
        request.set_kind(static_cast<int>(ETabletActionKind::Reshard));
        ToProto(request.mutable_tablet_ids(), tabletIds);
        request.set_tablet_count(newTabletCount);

        const auto& hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
        CreateMutation(hydraManager, request)
            ->CommitAndLog(Logger);
    }

    void SplitTablet(TTablet* tablet)
    {
        i64 desiredSize = tablet->GetInMemoryMode() == EInMemoryMode::None
            ? Config_->DesiredTabletSize
            : Config_->DesiredInMemoryTabletSize;

        int newTabletCount = DivCeil(GetTabletSize(tablet), desiredSize);

        if (newTabletCount < 2) {
            return;
        }

        LOG_DEBUG("Tablet balancer would like to reshard tablet (TabletId: %v, NewTabletCount: %v)",
            tablet->GetId(),
            newTabletCount);

        TReqCreateTabletAction request;
        request.set_kind(static_cast<int>(ETabletActionKind::Reshard));
        ToProto(request.mutable_tablet_ids(), std::vector<TTabletId>{tablet->GetId()});
        request.set_tablet_count(newTabletCount);

        const auto& hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
        CreateMutation(hydraManager, request)
            ->CommitAndLog(Logger);
    }


    void OnCheckEnabled()
    {
        const auto& worldInitializer = Bootstrap_->GetWorldInitializer();
        if (!worldInitializer->IsInitialized()) {
            return;
        }

        auto wasEnabled = Enabled_;

        try {
            if (Bootstrap_->IsPrimaryMaster()) {
                Enabled_ = OnCheckEnabledPrimary();
            } else {
                Enabled_ = false;
            }
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Error updating tablet balancer state, disabling until the next attempt");
            Enabled_ = false;
        }

        if (Enabled_ && !wasEnabled) {
            LOG_INFO("Tablet balancer enabled");
        }
    }

    bool OnCheckEnabledPrimary()
    {
        bool enabled = true;
        const auto& cypressManager = Bootstrap_->GetCypressManager();
        auto resolver = cypressManager->CreateResolver();
        auto sysNode = resolver->ResolvePath("//sys");
        if (sysNode->Attributes().Get<bool>("disable_tablet_balancer", false)) {
            if (Enabled_) {
                LOG_INFO("Tablet balancer is disabled by //sys/@disable_tablet_balancer setting");
            }
            enabled = false;
        }
        return enabled ? OnCheckEnabledWorkHours() : false;

    }

    bool OnCheckEnabledWorkHours()
    {
        bool enabled = true;
        const auto& cypressManager = Bootstrap_->GetCypressManager();
        auto resolver = cypressManager->CreateResolver();
        auto sysNode = resolver->ResolvePath("//sys");
        auto officeHours = sysNode->Attributes().Find<std::vector<int>>("tablet_balancer_office_hours");
        if (!officeHours) {
            return enabled;
        }
        if (officeHours->size() != 2) {
            LOG_INFO("Expected two integers in //sys/@tablet_balancer_office_hours, but got %v",
                *officeHours);
            return enabled;
        }

        tm localTime;
        Now().LocalTime(&localTime);
        int hour = localTime.tm_hour;
        if (hour < (*officeHours)[0] || hour > (*officeHours)[1]) {
            if (Enabled_) {
                LOG_INFO("Tablet balancer is disabled by //sys/@tablet_balancer_office_hours");
            }
            enabled = false;
        }
        return enabled;
    }
};

////////////////////////////////////////////////////////////////////////////////

TTabletBalancer::TTabletBalancer(
    TTabletBalancerConfigPtr config,
    NCellMaster::TBootstrap* bootstrap)
    : Impl_(New<TImpl>(std::move(config), bootstrap))
{ }

TTabletBalancer::~TTabletBalancer() = default;

void TTabletBalancer::Start()
{
    Impl_->Start();
}

void TTabletBalancer::Stop()
{
    Impl_->Stop();
}

void TTabletBalancer::OnTabletHeartbeat(TTablet* tablet)
{
    Impl_->OnTabletHeartbeat(tablet);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletServer
} // namespace NYT

