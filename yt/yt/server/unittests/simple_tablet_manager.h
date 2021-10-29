#pragma once

#include "tablet_context_mock.h"

#include <yt/yt/server/node/tablet_node/automaton.h>
#include <yt/yt/server/node/tablet_node/tablet_write_manager.h>
#include <yt/yt/server/node/tablet_node/tablet.h>

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/ytlib/tablet_client/config.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

struct TTabletOptions
{
    NTableClient::TTableSchemaPtr Schema = New<TTableSchema>(std::vector{
        TColumnSchema(TColumnSchema("k", EValueType::Int64).SetSortOrder(NTableClient::ESortOrder::Ascending)),
        TColumnSchema(TColumnSchema("v", EValueType::Int64)),
    });
    NTransactionClient::EAtomicity Atomicity = NTransactionClient::EAtomicity::Full;
    NTransactionClient::ECommitOrdering CommitOrdering = NTransactionClient::ECommitOrdering::Weak;
};

////////////////////////////////////////////////////////////////////////////////

class TSimpleTabletManager
    : public ITabletWriteManagerHost
    , public TTabletAutomatonPart
{
public:
    TSimpleTabletManager(
        TTransactionManagerPtr transactionManager,
        NHydra::ISimpleHydraManagerPtr hydraManager,
        NHydra::TCompositeAutomatonPtr automaton,
        IInvokerPtr automatonInvoker);
    void InitializeTablet(TTabletOptions options);
    void InitializeStoreManager();

    // ITabletWriteManagerHost

    i64 LockTablet(TTablet* tablet) override;
    i64 UnlockTablet(TTablet* tablet) override;
    TTablet* GetTabletOrThrow(TTabletId id) override;
    TTablet* FindTablet(const TTabletId& id) const override;
    TTransactionManagerPtr GetTransactionManager() const override;
    NTabletClient::TDynamicTabletCellOptionsPtr GetDynamicOptions() const override;
    TTabletManagerConfigPtr GetConfig() const override;
    void ValidateMemoryLimit(const std::optional<TString>& /*poolTag*/) override;
    TTimestamp GetLatestTimestamp() const override;
    bool ValidateAndDiscardRowRef(const TSortedDynamicRowRef& /*rowRef*/) override;
    void CheckIfTabletFullyUnlocked(TTablet* /*tablet*/) override;
    void UnlockLockedTablets(TTransaction* /*transaction*/) override;
    void AdvanceReplicatedTrimmedRowCount(TTablet* /*tablet*/, TTransaction* /*transaction*/) override;
    TCellId GetCellId() const override;

    TTablet* Tablet();

private:
    std::unique_ptr<TTablet> Tablet_;

    const TTransactionManagerPtr TransactionManager_;
    NTabletClient::TDynamicTabletCellOptionsPtr DynamicOptions_ = New<NTabletClient::TDynamicTabletCellOptions>();
    TTabletManagerConfigPtr Config_ = New<TTabletManagerConfig>();

    IStoreManagerPtr StoreManager_;
    TTabletContextMock TabletContext_;

    void LoadValues(TLoadContext& context);
    void LoadAsync(TLoadContext& context);
    void SaveValues(TSaveContext& context);
    TCallback<void(TSaveContext&)> SaveAsync();
    void Clear() override;
};

DECLARE_REFCOUNTED_CLASS(TSimpleTabletManager)
DEFINE_REFCOUNTED_TYPE(TSimpleTabletManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
