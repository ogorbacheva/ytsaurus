#include "slot_manager.h"

#include "bootstrap.h"
#include "private.h"
#include "slot_provider.h"
#include "tablet_slot.h"
#include "structured_logger.h"

#include <yt/yt/server/node/cluster_node/config.h>

#include <yt/yt/server/lib/cellar_agent/cellar_manager.h>
#include <yt/yt/server/lib/cellar_agent/cellar.h>
#include <yt/yt/server/lib/cellar_agent/occupant.h>

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/ytlib/tablet_client/config.h>

#include <yt/yt/ytlib/api/native/connection.h>

#include <yt/yt/client/transaction_client/timestamp_provider.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NTabletNode {

using namespace NConcurrency;
using namespace NCellarAgent;
using namespace NCellarClient;
using namespace NClusterNode;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TabletNodeLogger;

////////////////////////////////////////////////////////////////////////////////

class TSlotManager
    : public ISlotManager
{
public:
    explicit TSlotManager(IBootstrap* bootstrap)
        : Bootstrap_(bootstrap)
        , Config_(Bootstrap_->GetConfig()->TabletNode)
        , SlotScanExecutor_(New<TPeriodicExecutor>(
            Bootstrap_->GetControlInvoker(),
            BIND(&TSlotManager::OnScanSlots, Unretained(this)),
            Config_->SlotScanPeriod))
        , OrchidService_(CreateOrchidService())
    { }

    virtual void Initialize() override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto cellar = Bootstrap_->GetCellarManager()->GetCellar(ECellarType::Tablet);
        cellar->RegisterOccupierProvider(CreateTabletSlotOccupierProvider(Config_, Bootstrap_));

        cellar->SubscribeCreateOccupant(BIND(&TSlotManager::UpdateMemoryPoolWeights, MakeWeak(this)));
        cellar->SubscribeRemoveOccupant(BIND(&TSlotManager::UpdateMemoryPoolWeights, MakeWeak(this)));
        cellar->SubscribeUpdateOccupant(BIND(&TSlotManager::UpdateMemoryPoolWeights, MakeWeak(this)));

        SlotScanExecutor_->Start();
    }

    virtual bool IsOutOfMemory(const std::optional<TString>& poolTag) const override
    {
        const auto& tracker = Bootstrap_->GetMemoryUsageTracker();
        return tracker->IsExceeded(EMemoryCategory::TabletDynamic, poolTag);
    }

    virtual double GetUsedCpu(double cpuPerTabletSlot) const override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        double result = 0;
        for (const auto& occupant : Occupants()) {
            if (!occupant) {
                continue;
            }

            if (auto occupier = occupant->GetTypedOccupier<ITabletSlot>()) {
                result += occupier->GetUsedCpu(cpuPerTabletSlot);
            }
        }

        return result;
    }

    virtual ITabletSlotPtr FindSlot(NHydra::TCellId id) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        if (auto occupant = Bootstrap_->GetCellarManager()->GetCellar(ECellarType::Tablet)->FindOccupant(id)) {
            return occupant->GetTypedOccupier<ITabletSlot>();
        }
        return nullptr;
    }

    virtual const IYPathServicePtr& GetOrchidService() const override
    {
        return OrchidService_;
    }

    DEFINE_SIGNAL_OVERRIDE(void(), BeginSlotScan);
    DEFINE_SIGNAL_OVERRIDE(void(ITabletSlotPtr), ScanSlot);
    DEFINE_SIGNAL_OVERRIDE(void(), EndSlotScan);

private:
    IBootstrap* const Bootstrap_;
    const TTabletNodeConfigPtr Config_;
    const TPeriodicExecutorPtr SlotScanExecutor_;
    const IYPathServicePtr OrchidService_;

    using TBundlesMemoryPoolWeights = THashMap<TString, int>;
    TBundlesMemoryPoolWeights BundlesMemoryPoolWeights_;

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);

    TCompositeMapServicePtr CreateOrchidService()
    {
        return New<TCompositeMapService>()
            ->AddChild("dynamic_memory_pool_weights", IYPathService::FromMethod(
                &TSlotManager::GetDynamicMemoryPoolWeightsOrchid,
                MakeWeak(this)));
    }

    void GetDynamicMemoryPoolWeightsOrchid(IYsonConsumer* consumer) const
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        BuildYsonFluently(consumer)
            .DoMapFor(BundlesMemoryPoolWeights_, [] (TFluentMap fluent, const auto& pair) {
                fluent
                    .Item(pair.first).Value(pair.second);
            });
    }

    void UpdateMemoryPoolWeights()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        const auto& memoryTracker = Bootstrap_->GetMemoryUsageTracker();

        auto update = [&] (const TString& bundleName, int weight) {
            YT_LOG_DEBUG("Tablet cell bundle memory pool weight updated (Bundle: %v, Weight: %v)",
                bundleName,
                weight);
            memoryTracker->SetPoolWeight(bundleName, weight);
        };

        TBundlesMemoryPoolWeights weights;
        for (const auto& occupant : Occupants()) {
            if (occupant) {
                weights[occupant->GetCellBundleName()] += occupant->GetDynamicOptions()->DynamicMemoryPoolWeight;
            }
        }

        for (const auto& [bundle, weight] : weights) {
            if (auto it = BundlesMemoryPoolWeights_.find(bundle); !it || it->second != weight) {
                update(bundle, weight);
            }
        }
        for (const auto& [bundle, _] : BundlesMemoryPoolWeights_) {
            if (!weights.contains(bundle)) {
                update(bundle, 0);
            }
        }

        BundlesMemoryPoolWeights_ = std::move(weights);
    }

    void OnScanSlots()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        YT_LOG_DEBUG("Slot scan started");

        Bootstrap_->GetStructuredLogger()->LogEvent("begin_slot_scan");

        BeginSlotScan_.Fire();

        std::vector<TFuture<void>> asyncResults;
        for (const auto& occupant : Occupants()) {
            if (!occupant) {
                continue;
            }

            auto occupier = occupant->GetTypedOccupier<ITabletSlot>();
            if (!occupier) {
                continue;
            }

            asyncResults.push_back(
                BIND([=, this_ = MakeStrong(this)] () {
                    ScanSlot_.Fire(occupier);
                })
                .AsyncVia(occupier->GetGuardedAutomatonInvoker())
                .Run()
                // Silent any error to avoid premature return from WaitFor.
                .Apply(BIND([] (const TError&) { })));
        }
        auto result = WaitFor(AllSucceeded(asyncResults));
        YT_VERIFY(result.IsOK());

        EndSlotScan_.Fire();

        Bootstrap_->GetStructuredLogger()->LogEvent("end_slot_scan");

        YT_LOG_DEBUG("Slot scan completed");
    }

    const std::vector<ICellarOccupantPtr>& Occupants() const
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return Bootstrap_->GetCellarManager()->GetCellar(ECellarType::Tablet)->Occupants();
    }
};

ISlotManagerPtr CreateSlotManager(IBootstrap* bootstrap)
{
    return New<TSlotManager>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode::NYT
