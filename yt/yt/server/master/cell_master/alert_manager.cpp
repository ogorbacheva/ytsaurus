#include "alert_manager.h"

#include "private.h"
#include "automaton.h"
#include "config.h"
#include "config_manager.h"
#include "hydra_facade.h"
#include "multicell_manager.h"

// COMPAT(gritukan)
#include "serialize.h"

#include <yt/server/master/cell_master/proto/alert_manager.pb.h>

#include <yt/core/concurrency/periodic_executor.h>

namespace NYT::NCellMaster {

using namespace NCellMaster::NProto;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NHydra;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = CellMasterLogger;

////////////////////////////////////////////////////////////////////////////////

class TAlertManager::TImpl
    : public TMasterAutomatonPart
{
public:
    explicit TImpl(TBootstrap* bootstrap)
        : TMasterAutomatonPart(bootstrap, EAutomatonThreadQueue::Default)
        , UpdateAlertsExecutor_(New<TPeriodicExecutor>(
            Bootstrap_->GetHydraFacade()->GetAutomatonInvoker(EAutomatonThreadQueue::Periodic),
            BIND(&TImpl::UpdateAlerts, MakeWeak(this))))
    {
        VERIFY_INVOKER_THREAD_AFFINITY(Bootstrap_->GetHydraFacade()->GetAutomatonInvoker(EAutomatonThreadQueue::Default), AutomatonThread);

        RegisterLoader(
            "AlertManager",
            BIND(&TImpl::Load, Unretained(this)));

        RegisterSaver(
            ESyncSerializationPriority::Values,
            "AlertManager",
            BIND(&TImpl::Save, Unretained(this)));

        RegisterMethod(BIND(&TImpl::HydraSetCellAlerts, Unretained(this)));
    }

    void Initialize()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        Bootstrap_->GetConfigManager()->SubscribeConfigChanged(BIND(&TImpl::OnDynamicConfigChanged, MakeWeak(this)));
    }

    void RegisterAlertSource(TAlertSource alertSource)
    {
        VERIFY_THREAD_AFFINITY_ANY();

        AlertSources_.push_back(alertSource);
    }

    std::vector<TError> GetAlerts() const
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        YT_VERIFY(multicellManager->IsPrimaryMaster());

        std::vector<TError> alerts;
        for (const auto& [cellTag, cellAlerts] : CellTagToAlerts_) {
            alerts.insert(alerts.end(), cellAlerts.begin(), cellAlerts.end());
        }

        return alerts;
    }

private:
    const TPeriodicExecutorPtr UpdateAlertsExecutor_;

    THashMap<NObjectServer::TCellTag, std::vector<TError>> CellTagToAlerts_;

    std::vector<TAlertSource> AlertSources_;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);

    void HydraSetCellAlerts(TReqSetCellAlerts* request)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        YT_VERIFY(multicellManager->IsPrimaryMaster());

        auto cellTag = request->cell_tag();
        auto alerts = FromProto<std::vector<TError>>(request->alerts());

        if (cellTag == multicellManager->GetCellTag()) {
            YT_LOG_DEBUG_IF(IsMutationLoggingEnabled(),
                "Updating primary master alerts (CellTag: %v, AlertCount: %v)",
                cellTag,
                request->alerts_size());
        } else {
            YT_LOG_DEBUG_IF(IsMutationLoggingEnabled(),
                "Received alerts from secondary master (CellTag: %v, AlertCount: %v)",
                cellTag,
                request->alerts_size());
        }

        CellTagToAlerts_[cellTag] = std::move(alerts);
    }

    virtual void OnLeaderActive() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TMasterAutomatonPart::OnLeaderActive();

        OnDynamicConfigChanged();

        UpdateAlertsExecutor_->Start();
    }

    virtual void OnStopLeading() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TMasterAutomatonPart::OnStopLeading();

        UpdateAlertsExecutor_->Stop();
    }

    void OnDynamicConfigChanged(TDynamicClusterConfigPtr/* oldConfig */ = nullptr)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        UpdateAlertsExecutor_->SetPeriod(
            Bootstrap_->GetConfigManager()->GetConfig()->CellMaster->AlertUpdatePeriod);
    }

    void Load(TLoadContext& context)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        using NYT::Load;

        // COMPAT(gritukan)
        if (context.GetVersion() >= EMasterReign::MasterAlerts) {
            Load(context, CellTagToAlerts_);
        }
    }

    void Save(TSaveContext& context)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        using NYT::Save;

        Save(context, CellTagToAlerts_);
    }

    void UpdateAlerts()
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        YT_LOG_DEBUG("Updating master alerts");

        std::vector<TError> localAlerts;
        for (const auto& alertSource : AlertSources_) {
            auto alerts = alertSource();
            localAlerts.insert(localAlerts.end(), alerts.begin(), alerts.end());
        }

        for (const auto& alert : localAlerts) {
            YT_VERIFY(!alert.IsOK());
            YT_LOG_WARNING(alert, "Registered master alert");
        }

        const auto& multicellManager = Bootstrap_->GetMulticellManager();

        TReqSetCellAlerts request;
        request.set_cell_tag(multicellManager->GetCellTag());
        ToProto(request.mutable_alerts(), localAlerts);

        if (multicellManager->IsPrimaryMaster()) {
            const auto& hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
            CreateMutation(hydraManager, request)
                ->CommitAndLog(Logger);
        } else {
            multicellManager->PostToPrimaryMaster(request, /* reliable */ false);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TAlertManager::TAlertManager(TBootstrap* bootstrap)
    : Impl_(New<TImpl>(bootstrap))
{ }

TAlertManager::~TAlertManager() = default;

void TAlertManager::Initialize()
{
    Impl_->Initialize();
}

void TAlertManager::RegisterAlertSource(TAlertSource alertSource)
{
    Impl_->RegisterAlertSource(alertSource);
}

std::vector<TError> TAlertManager::GetAlerts() const
{
    return Impl_->GetAlerts();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellMaster
