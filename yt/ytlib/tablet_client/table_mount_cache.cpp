#include "table_mount_cache.h"
#include "private.h"
#include "config.h"

#include <yt/ytlib/cypress_client/cypress_ypath_proxy.h>

#include <yt/ytlib/election/config.h>

#include <yt/ytlib/hive/cell_directory.h>

#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/query_client/folding_profiler.h>
#include <yt/ytlib/query_client/cg_types.h>
#include <yt/ytlib/query_client/cg_fragment_compiler.h>
#include <yt/ytlib/query_client/query_statistics.h>

#include <yt/ytlib/table_client/table_ypath_proxy.h>
#include <yt/ytlib/table_client/unversioned_row.h>

#include <yt/ytlib/tablet_client/public.h>

#include <yt/core/concurrency/delayed_executor.h>
#include <yt/core/concurrency/rw_spinlock.h>

#include <yt/core/misc/expiring_cache.h>
#include <yt/core/misc/string.h>

#include <yt/core/ytree/ypath.pb.h>

#include <util/datetime/base.h>

////////////////////////////////////////////////////////////////////////////////

namespace NYT {
namespace NTabletClient {

using namespace NConcurrency;
using namespace NYTree;
using namespace NYTree::NProto;
using namespace NYPath;
using namespace NRpc;
using namespace NElection;
using namespace NObjectClient;
using namespace NCypressClient;
using namespace NTableClient;
using namespace NHive;
using namespace NNodeTrackerClient;
using namespace NQueryClient;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TabletClientLogger;

////////////////////////////////////////////////////////////////////////////////

class TTabletCache
{
public:
    TTabletInfoPtr Find(const TTabletId& tabletId)
    {
        TReaderGuard guard(SpinLock_);
        RemoveExpiredEntries();
        auto it = Map_.find(tabletId);
        return it != Map_.end() ? it->second.Lock() : nullptr;
    }

    TTabletInfoPtr Insert(TTabletInfoPtr tabletInfo)
    {
        TWriterGuard guard(SpinLock_);
        auto it = Map_.find(tabletInfo->TabletId);
        if (it != Map_.end()) {
            if (auto existingTabletInfo = it->second.Lock()) {
                if (tabletInfo->MountRevision < existingTabletInfo->MountRevision) {
                    tabletInfo.Swap(existingTabletInfo);
                }
                it->second = MakeWeak(tabletInfo);
                tabletInfo->Owners.insert(
                    tabletInfo->Owners.end(),
                    existingTabletInfo->Owners.begin(),
                    existingTabletInfo->Owners.end());
            } else {
                it->second = MakeWeak(tabletInfo);
            }
        } else {
            YCHECK(Map_.insert({tabletInfo->TabletId, tabletInfo}).second);
        }
        tabletInfo->UpdateTime = Now();
        return tabletInfo;
    }

private:
    yhash_map<TTabletId, TWeakPtr<TTabletInfo>> Map_;
    TReaderWriterSpinLock SpinLock_;
    TInstant LastExpiredRemovalTime_;
    static const TDuration ExpiringTimeout_;

    void RemoveExpiredEntries()
    {
        if (LastExpiredRemovalTime_ + ExpiringTimeout_ < Now()) {
            return;
        }

        std::vector<TTabletId> removeIds;
        for (auto it = Map_.begin(); it != Map_.end(); ++it) {
            if (it->second.IsExpired()) {
                removeIds.push_back(it->first);
            }
        }
        for (const auto& tabletId : removeIds) {
            Map_.erase(tabletId);
        }

        LastExpiredRemovalTime_ = Now();
    }
};

const TDuration TTabletCache::ExpiringTimeout_ = TDuration::Seconds(1);

////////////////////////////////////////////////////////////////////////////////

TTabletInfoPtr TTableMountInfo::GetTablet(TUnversionedRow row) const
{
    if (Tablets.empty()) {
        THROW_ERROR_EXCEPTION("Table %v has no tablets",
            Path);
    }
    auto it = std::upper_bound(
        Tablets.begin(),
        Tablets.end(),
        row,
        [&] (TUnversionedRow lhs, const TTabletInfoPtr& rhs) {
            return CompareRows(lhs, rhs->PivotKey.Get(), KeyColumns.size()) < 0;
        });
    return *(it - 1);
}

void TTableMountInfo::ValidateDynamic() const
{
    if (!Dynamic) {
        THROW_ERROR_EXCEPTION("Table %v is not dynamic", Path);
    }
}

////////////////////////////////////////////////////////////////////////////////

class TTableMountCache::TImpl
    : public TExpiringCache<TYPath, TTableMountInfoPtr>
{
public:
    TImpl(
        TTableMountCacheConfigPtr config,
        IChannelPtr masterChannel,
        TCellDirectoryPtr cellDirectory)
        : TExpiringCache(config)
        , Config_(config)
        , ObjectProxy_(masterChannel)
        , CellDirectory_(cellDirectory)
    { }

    TFuture<TTableMountInfoPtr> GetTableInfo(const TYPath& path)
    {
        return TExpiringCache::Get(path);
    }

    TTabletInfoPtr FindTablet(const TTabletId& tabletId)
    {
        return TabletCache_.Find(tabletId);
    }

    void InvalidateTablet(TTabletInfoPtr tabletInfo)
    {
        for (const auto& weakOwner : tabletInfo->Owners) {
            if (auto owner = weakOwner.Lock()) {
                TryRemove(owner->Path);
            }
        }
    }

    void Clear()
    {
        TExpiringCache::Clear();
        LOG_DEBUG("Table mount info cache cleared");
    }

private:
    const TTableMountCacheConfigPtr Config_;
    TObjectServiceProxy ObjectProxy_;
    const TCellDirectoryPtr CellDirectory_;
    TTabletCache TabletCache_;

    virtual TFuture<TTableMountInfoPtr> DoGet(const TYPath& path) override
    {
        LOG_DEBUG("Requesting table mount info (Path: %v)",
            path);

        auto req = TTableYPathProxy::GetMountInfo(path);
        auto* cachingHeaderExt = req->Header().MutableExtension(TCachingHeaderExt::caching_header_ext);
        cachingHeaderExt->set_success_expiration_time(ToProto(Config_->SuccessExpirationTime));
        cachingHeaderExt->set_failure_expiration_time(ToProto(Config_->FailureExpirationTime));

        return ObjectProxy_.Execute(req).Apply(
            BIND([= , this_ = MakeStrong(this)] (const TTableYPathProxy::TErrorOrRspGetMountInfoPtr& rspOrError) {
                if (!rspOrError.IsOK()) {
                    auto wrappedError = TError("Error getting mount info for %v",
                        path)
                        << rspOrError;
                    LOG_WARNING(wrappedError);
                    THROW_ERROR wrappedError;
                }

                const auto& rsp = rspOrError.Value();
                auto tableInfo = New<TTableMountInfo>();
                tableInfo->Path = path;
                tableInfo->TableId = FromProto<TObjectId>(rsp->table_id());
                tableInfo->Schema = FromProto<TTableSchema>(rsp->schema());
                tableInfo->KeyColumns = FromProto<TKeyColumns>(rsp->key_columns());
                tableInfo->Sorted = rsp->sorted();
                tableInfo->Dynamic = rsp->dynamic();
                tableInfo->NeedKeyEvaluation = tableInfo->Schema.HasComputedColumns();

                for (const auto& protoTabletInfo : rsp->tablets()) {
                    auto tabletInfo = New<TTabletInfo>();
                    tabletInfo->CellId = FromProto<TCellId>(protoTabletInfo.cell_id());
                    tabletInfo->TabletId = FromProto<TObjectId>(protoTabletInfo.tablet_id());
                    tabletInfo->MountRevision = protoTabletInfo.mount_revision();
                    tabletInfo->State = ETabletState(protoTabletInfo.state());
                    tabletInfo->PivotKey = FromProto<TOwningKey>(protoTabletInfo.pivot_key());

                    if (protoTabletInfo.has_cell_id()) {
                        tabletInfo->CellId = FromProto<TCellId>(protoTabletInfo.cell_id());
                    }

                    tabletInfo->Owners.push_back(MakeWeak(tableInfo));

                    tabletInfo = TabletCache_.Insert(std::move(tabletInfo));
                    tableInfo->Tablets.push_back(tabletInfo);
                }

                for (const auto& protoDescriptor : rsp->tablet_cells()) {
                    auto descriptor = FromProto<TCellDescriptor>(protoDescriptor);
                    if (CellDirectory_->ReconfigureCell(descriptor)) {
                        LOG_DEBUG("Hive cell reconfigured (CellId: %v, ConfigVersion: %v)",
                            descriptor.CellId,
                            descriptor.ConfigVersion);
                    }
                }

                LOG_DEBUG("Table mount info received (Path: %v, TableId: %v, TabletCount: %v, Sorted: %v, Dynamic: %v)",
                    path,
                    tableInfo->TableId,
                    tableInfo->Tablets.size(),
                    tableInfo->Sorted,
                    tableInfo->Dynamic);

                return tableInfo;
            }));
    }

};

////////////////////////////////////////////////////////////////////////////////

TTableMountCache::TTableMountCache(
    TTableMountCacheConfigPtr config,
    IChannelPtr masterChannel,
    TCellDirectoryPtr cellDirectory)
    : Impl_(New<TImpl>(
        config,
        masterChannel,
        cellDirectory))
{ }

TTableMountCache::~TTableMountCache()
{ }

TFuture<TTableMountInfoPtr> TTableMountCache::GetTableInfo(const TYPath& path)
{
    return Impl_->GetTableInfo(path);
}

TTabletInfoPtr TTableMountCache::FindTablet(const TTabletId& tabletId)
{
    return Impl_->FindTablet(tabletId);
}

void TTableMountCache::InvalidateTablet(TTabletInfoPtr tabletInfo)
{
    Impl_->InvalidateTablet(std::move(tabletInfo));
}

void TTableMountCache::Clear()
{
    Impl_->Clear();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletClient
} // namespace NYT

