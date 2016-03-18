#pragma once

#include "store_manager_detail.h"
#include "dynamic_store_bits.h"

#include <yt/server/cell_node/public.h>

#include <yt/ytlib/table_client/public.h>

#include <yt/ytlib/tablet_client/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TSortedStoreManager
    : public TStoreManagerBase
{
public:
    TSortedStoreManager(
        TTabletManagerConfigPtr config,
        TTablet* tablet,
        ITabletContext* tabletContext,
        NHydra::IHydraManagerPtr hydraManager,
        TInMemoryManagerPtr inMemoryManager);

    virtual void ExecuteAtomicWrite(
        TTablet* tablet,
        TTransaction* transaction,
        NTabletClient::TWireProtocolReader* reader,
        bool prelock) override;
    virtual void ExecuteNonAtomicWrite(
        TTablet* tablet,
        NTransactionClient::TTimestamp commitTimestamp,
        NTabletClient::TWireProtocolReader* reader) override;

    TSortedDynamicRowRef WriteRowAtomic(
        TTransaction* transaction,
        TUnversionedRow row,
        bool prelock);
    void WriteRowNonAtomic(
        TTimestamp commitTimestamp,
        TUnversionedRow row);
    TSortedDynamicRowRef DeleteRowAtomic(
        TTransaction* transaction,
        TKey key,
        bool prelock);
    void DeleteRowNonAtomic(
        TTimestamp commitTimestamp,
        TKey key);

    static void LockRow(TTransaction* transaction, bool prelock, const TSortedDynamicRowRef& rowRef);
    void ConfirmRow(TTransaction* transaction, const TSortedDynamicRowRef& rowRef);
    void PrepareRow(TTransaction* transaction, const TSortedDynamicRowRef& rowRef);
    void CommitRow(TTransaction* transaction, const TSortedDynamicRowRef& rowRef);
    void AbortRow(TTransaction* transaction, const TSortedDynamicRowRef& rowRef);

    virtual void AddStore(IStorePtr store, bool onMount) override;
    virtual void RemoveStore(IStorePtr store) override;

    virtual void CreateActiveStore() override;

    virtual bool IsStoreCompactable(IStorePtr store) const override;

private:
    const int KeyColumnCount_;

    TSortedDynamicStorePtr ActiveStore_;
    std::multimap<TTimestamp, ISortedStorePtr> MaxTimestampToStore_;


    virtual IDynamicStore* GetActiveStore() const override;
    virtual void ResetActiveStore() override;
    virtual void OnActiveStoreRotated() override;

    ui32 ComputeLockMask(TUnversionedRow row);

    void CheckInactiveStoresLocks(
        TTransaction* transaction,
        TUnversionedRow row,
        ui32 lockMask);

    void ValidateOnWrite(const TTransactionId& transactionId, TUnversionedRow row);
    void ValidateOnDelete(const TTransactionId& transactionId, TKey key);

};

DEFINE_REFCOUNTED_TYPE(TSortedStoreManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
