#include "table_node_proxy_detail.h"
#include "private.h"
#include "table_node.h"
#include "replicated_table_node.h"

#include <yt/server/master/cell_master/bootstrap.h>
#include <yt/server/master/cell_master/config.h>
#include <yt/server/master/cell_master/config_manager.h>
#include <yt/server/master/cell_master/hydra_facade.h>

#include <yt/server/master/chunk_server/chunk.h>
#include <yt/server/master/chunk_server/chunk_list.h>
#include <yt/server/master/chunk_server/chunk_visitor.h>

#include <yt/server/master/node_tracker_server/node_directory_builder.h>

#include <yt/server/master/table_server/shared_table_schema.h>

#include <yt/server/master/tablet_server/tablet.h>
#include <yt/server/master/tablet_server/tablet_cell.h>
#include <yt/server/master/tablet_server/table_replica.h>
#include <yt/server/master/tablet_server/tablet_manager.h>

#include <yt/server/master/security_server/security_manager.h>

#include <yt/server/lib/misc/interned_attributes.h>

#include <yt/server/master/object_server/object_manager.h>

#include <yt/client/chunk_client/read_limit.h>

#include <yt/ytlib/api/native/client.h>
#include <yt/ytlib/api/native/config.h>
#include <yt/ytlib/api/native/connection.h>

#include <yt/ytlib/table_client/schema.h>

#include <yt/ytlib/tablet_client/config.h>

#include <yt/ytlib/transaction_client/helpers.h>

#include <yt/client/transaction_client/timestamp_provider.h>

#include <yt/core/erasure/codec.h>

#include <yt/core/misc/serialize.h>
#include <yt/core/misc/string.h>

#include <yt/core/ypath/token.h>

#include <yt/core/ytree/ephemeral_node_factory.h>
#include <yt/core/ytree/tree_builder.h>
#include <yt/core/ytree/fluent.h>

#include <yt/core/yson/async_consumer.h>

namespace NYT::NTableServer {

using namespace NApi;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NConcurrency;
using namespace NCypressServer;
using namespace NNodeTrackerServer;
using namespace NObjectServer;
using namespace NRpc;
using namespace NSecurityServer;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTabletServer;
using namespace NTransactionServer;
using namespace NYTree;
using namespace NYson;

using NChunkClient::TReadLimit;

////////////////////////////////////////////////////////////////////////////////

TTableNodeProxy::TTableNodeProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TTransaction* transaction,
    TTableNode* trunkNode)
    : TBase(
        bootstrap,
        metadata,
        transaction,
        trunkNode)
{ }

void TTableNodeProxy::GetBasicAttributes(TGetBasicAttributesContext* context)
{
    if (context->Permission == EPermission::Read) {
        // We shall take care of reads ourselves.
        TPermissionCheckOptions checkOptions;
        auto* table = GetThisImpl();
        if (context->Columns) {
            checkOptions.Columns = std::move(context->Columns);
        } else {
            const auto& tableSchema = table->GetTableSchema();
            checkOptions.Columns.emplace();
            checkOptions.Columns->reserve(tableSchema.Columns().size());
            for (const auto& columnSchema : tableSchema.Columns()) {
                checkOptions.Columns->push_back(columnSchema.Name());
            }
        }

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* user = securityManager->GetAuthenticatedUser();
        auto checkResponse = securityManager->CheckPermission(
            Object_,
            user,
            EPermission::Read,
            checkOptions);

        if (checkResponse.Action == ESecurityAction::Deny) {
            TPermissionCheckTarget target;
            target.Object = Object_;
            securityManager->LogAndThrowAuthorizationError(
                target,
                user,
                EPermission::Read,
                checkResponse);
        }

        if (checkOptions.Columns) {
            for (size_t index = 0; index < checkOptions.Columns->size(); ++index) {
                const auto& column = (*checkOptions.Columns)[index];
                const auto& result = (*checkResponse.Columns)[index];
                if (result.Action == ESecurityAction::Deny) {
                    if (context->OmitInaccessibleColumns) {
                        context->OmittedInaccessibleColumns.emplace().push_back(column);
                    } else {
                        TPermissionCheckTarget target;
                        target.Object = Object_;
                        target.Column = column;
                        securityManager->LogAndThrowAuthorizationError(
                            target,
                            user,
                            EPermission::Read,
                            result);
                    }
                }
            }
        }

        // No need for an extra check below.
        context->Permission = std::nullopt;
    }

    TBase::GetBasicAttributes(context);
}

void TTableNodeProxy::ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors)
{
    TBase::ListSystemAttributes(descriptors);

    const auto* table = GetThisImpl();
    const auto* trunkTable = table->GetTrunkNode();
    bool isDynamic = table->IsDynamic();
    bool isSorted = table->IsSorted();
    bool isExternal = table->IsExternal();

    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ChunkRowCount));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::RowCount)
        .SetPresent(!isDynamic));
    // TODO(savrus) remove "unmerged_row_count" in 20.0
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::UnmergedRowCount)
        .SetPresent(isDynamic && isSorted));
    descriptors->push_back(EInternedAttributeKey::Sorted);
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::KeyColumns)
        .SetReplicated(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Schema)
        .SetReplicated(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::SchemaDuplicateCount));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::SortedBy)
        .SetPresent(isSorted));
    descriptors->push_back(EInternedAttributeKey::Dynamic);
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletCount)
        .SetExternal(isExternal)
        .SetPresent(isDynamic));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletState)
        .SetPresent(isDynamic));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ActualTabletState)
        .SetPresent(isDynamic));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ExpectedTabletState)
        .SetPresent(isDynamic));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::CurrentMountTransactionId)
        .SetPresent(isDynamic && trunkTable->GetCurrentMountTransactionId()));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::LastMountTransactionId)
        .SetPresent(isDynamic && trunkTable->GetLastMountTransactionId()));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::LastCommitTimestamp)
        .SetExternal(isExternal)
        .SetPresent(isDynamic && isSorted));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Tablets)
        .SetExternal(isExternal)
        .SetPresent(isDynamic)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletCountByState)
        .SetExternal(isExternal)
        .SetPresent(isDynamic)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletCountByExpectedState)
        .SetExternal(isExternal)
        .SetPresent(isDynamic)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::PivotKeys)
        .SetExternal(isExternal)
        .SetPresent(isDynamic && isSorted)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::RetainedTimestamp)
        .SetExternal(isExternal)
        .SetPresent(isDynamic && isSorted));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::UnflushedTimestamp)
        .SetExternal(isExternal)
        .SetPresent(isDynamic && isSorted));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletStatistics)
        .SetExternal(isExternal)
        .SetPresent(isDynamic)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletErrors)
        .SetExternal(isExternal)
        .SetPresent(isDynamic)
        .SetExternal(isExternal)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletErrorsUntrimmed)
        .SetPresent(isDynamic)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletErrorCount)
        .SetExternal(isExternal)
        .SetPresent(isDynamic));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletCellBundle)
        .SetWritable(true)
        .SetPresent(trunkTable->GetTabletCellBundle())
        .SetReplicated(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Atomicity)
        .SetReplicated(true)
        .SetWritable(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::CommitOrdering)
        .SetWritable(true)
        .SetPresent(!isSorted)
        .SetReplicated(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::InMemoryMode)
        .SetReplicated(true)
        .SetWritable(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::OptimizeFor)
        .SetReplicated(true)
        .SetWritable(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::OptimizeForStatistics)
        .SetExternal(isExternal)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::SchemaMode));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ChunkWriter)
        .SetCustom(true)
        .SetReplicated(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::UpstreamReplicaId)
        .SetExternal(isExternal)
        .SetPresent(isDynamic));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TableChunkFormatStatistics)
        .SetExternal(isExternal)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::EnableTabletBalancer)
        .SetWritable(true)
        .SetRemovable(true)
        .SetReplicated(true)
        .SetPresent(static_cast<bool>(table->GetEnableTabletBalancer())));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::DisableTabletBalancer)
        .SetWritable(true)
        .SetRemovable(true)
        .SetReplicated(true)
        .SetPresent(static_cast<bool>(table->GetEnableTabletBalancer())));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::MinTabletSize)
        .SetWritable(true)
        .SetRemovable(true)
        .SetReplicated(true)
        .SetPresent(static_cast<bool>(table->GetMinTabletSize())));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::MaxTabletSize)
        .SetWritable(true)
        .SetRemovable(true)
        .SetReplicated(true)
        .SetPresent(static_cast<bool>(table->GetMaxTabletSize())));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::DesiredTabletSize)
        .SetWritable(true)
        .SetRemovable(true)
        .SetReplicated(true)
        .SetPresent(static_cast<bool>(table->GetDesiredTabletSize())));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::DesiredTabletCount)
        .SetWritable(true)
        .SetRemovable(true)
        .SetReplicated(true)
        .SetPresent(static_cast<bool>(table->GetDesiredTabletCount())));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ForcedCompactionRevision)
        .SetWritable(true)
        .SetRemovable(true)
        .SetReplicated(true)
        .SetPresent(static_cast<bool>(table->GetForcedCompactionRevision())));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::FlushLagTime)
        .SetExternal(isExternal)
        .SetPresent(isDynamic && isSorted));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TabletBalancerConfig)
        .SetWritable(true)
        .SetReplicated(true)
        .SetPresent(isDynamic));
}

bool TTableNodeProxy::GetBuiltinAttribute(TInternedAttributeKey key, IYsonConsumer* consumer)
{
    const auto* table = GetThisImpl();
    const auto* trunkTable = table->GetTrunkNode();
    auto statistics = table->ComputeTotalStatistics();
    bool isDynamic = table->IsDynamic();
    bool isSorted = table->IsSorted();
    bool isExternal = table->IsExternal();

    const auto& tabletManager = Bootstrap_->GetTabletManager();
    const auto& timestampProvider = Bootstrap_->GetTimestampProvider();
    const auto& chunkManager = Bootstrap_->GetChunkManager();

    switch (key) {
        case EInternedAttributeKey::ChunkRowCount:
            BuildYsonFluently(consumer)
                .Value(statistics.row_count());
            return true;

        case EInternedAttributeKey::RowCount:
            if (isDynamic) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(statistics.row_count());
            return true;

        case EInternedAttributeKey::UnmergedRowCount:
            if (!isDynamic || !isSorted) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(statistics.row_count());
            return true;

        case EInternedAttributeKey::Sorted:
            BuildYsonFluently(consumer)
                .Value(table->GetTableSchema().IsSorted());
            return true;

        case EInternedAttributeKey::KeyColumns:
            BuildYsonFluently(consumer)
                .Value(table->GetTableSchema().GetKeyColumns());
            return true;

        case EInternedAttributeKey::Schema:
            BuildYsonFluently(consumer)
                .Value(table->GetTableSchema());
            return true;

        case EInternedAttributeKey::SchemaDuplicateCount: {
            const auto& sharedSchema = table->SharedTableSchema();
            i64 duplicateCount = sharedSchema ? sharedSchema->GetRefCount() : 0;
            BuildYsonFluently(consumer)
                .Value(duplicateCount);
            return true;
        }

        case EInternedAttributeKey::SchemaMode:
            BuildYsonFluently(consumer)
                .Value(table->GetSchemaMode());
            return true;

        case EInternedAttributeKey::SortedBy:
            if (!isSorted) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(table->GetTableSchema().GetKeyColumns());
            return true;

        case EInternedAttributeKey::Dynamic:
            BuildYsonFluently(consumer)
                .Value(trunkTable->IsDynamic());
            return true;

        case EInternedAttributeKey::TabletCount:
            if (!isDynamic || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->Tablets().size());
            return true;

        case EInternedAttributeKey::TabletCountByState:
            if (!isDynamic || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->TabletCountByState());
            return true;

        case EInternedAttributeKey::TabletCountByExpectedState:
            if (!isDynamic || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->TabletCountByExpectedState());
            return true;

        case EInternedAttributeKey::TabletState:
            if (!isDynamic) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetTabletState());
            return true;

        case EInternedAttributeKey::ActualTabletState:
            if (!isDynamic) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetActualTabletState());
            return true;

        case EInternedAttributeKey::ExpectedTabletState:
            if (!isDynamic) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetExpectedTabletState());
            return true;

        case EInternedAttributeKey::CurrentMountTransactionId:
            if (!isDynamic || !trunkTable->GetCurrentMountTransactionId()) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetCurrentMountTransactionId());
            return true;

        case EInternedAttributeKey::LastMountTransactionId:
            if (!isDynamic || !trunkTable->GetLastMountTransactionId()) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetLastMountTransactionId());
            return true;

        case EInternedAttributeKey::LastCommitTimestamp:
            if (!isDynamic || !isSorted || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetLastCommitTimestamp());
            return true;

        case EInternedAttributeKey::Tablets:
            if (!isDynamic || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .DoListFor(trunkTable->Tablets(), [&] (TFluentList fluent, TTablet* tablet) {
                    auto* cell = tablet->GetCell();
                    fluent
                        .Item().BeginMap()
                            .Item("index").Value(tablet->GetIndex())
                            .Item("performance_counters").Value(tablet->PerformanceCounters())
                            .DoIf(table->IsSorted(), [&] (TFluentMap fluent) {
                                fluent
                                    .Item("pivot_key").Value(tablet->GetPivotKey());
                            })
                            .DoIf(!table->IsPhysicallySorted(), [&] (TFluentMap fluent) {
                                const auto* chunkList = tablet->GetChunkList();
                                fluent
                                    .Item("trimmed_row_count").Value(tablet->GetTrimmedRowCount())
                                    .Item("flushed_row_count").Value(chunkList->Statistics().LogicalRowCount);
                            })
                            .Item("state").Value(tablet->GetState())
                            .Item("last_commit_timestamp").Value(tablet->NodeStatistics().last_commit_timestamp())
                            .Item("statistics").Value(New<TSerializableTabletStatistics>(
                                tabletManager->GetTabletStatistics(tablet),
                                chunkManager))
                            .Item("tablet_id").Value(tablet->GetId())
                            .DoIf(cell, [&] (TFluentMap fluent) {
                                fluent.Item("cell_id").Value(cell->GetId());
                            })
                            .Item("error_count").Value(tablet->GetErrorCount())
                        .EndMap();
                });
            return true;

        case EInternedAttributeKey::PivotKeys:
            if (!isDynamic || !isSorted || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .DoListFor(trunkTable->Tablets(), [&] (TFluentList fluent, TTablet* tablet) {
                    fluent
                        .Item().Value(tablet->GetPivotKey());
                });
            return true;

        case EInternedAttributeKey::RetainedTimestamp:
            if (!isDynamic || !isSorted || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(table->GetCurrentRetainedTimestamp());
            return true;

        case EInternedAttributeKey::UnflushedTimestamp:
            if (!isDynamic || !isSorted || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(table->GetCurrentUnflushedTimestamp(timestampProvider->GetLatestTimestamp()));
            return true;

        case EInternedAttributeKey::TabletStatistics: {
            if (!isDynamic || isExternal) {
                break;
            }
            TTabletStatistics tabletStatistics;
            for (const auto& tablet : trunkTable->Tablets()) {
                tabletStatistics += tabletManager->GetTabletStatistics(tablet);
            }
            BuildYsonFluently(consumer)
                .Value(New<TSerializableTabletStatistics>(
                    tabletStatistics,
                    chunkManager));
            return true;
        }

        case EInternedAttributeKey::TabletErrors: {
            if (!isDynamic || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(table->GetTabletErrors(TabletErrorCountViewLimit));
            return true;
        }

        case EInternedAttributeKey::TabletErrorsUntrimmed: {
            if (!isDynamic) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(table->GetTabletErrors());
            return true;
        }

        case EInternedAttributeKey::TabletErrorCount:
            if (!isDynamic || isExternal) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetTabletErrorCount());
            return true;

        case EInternedAttributeKey::TabletCellBundle:
            if (!trunkTable->GetTabletCellBundle()) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetTabletCellBundle()->GetName());
            return true;

        case EInternedAttributeKey::Atomicity:
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetAtomicity());
            return true;

        case EInternedAttributeKey::CommitOrdering:
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetCommitOrdering());
            return true;

        case EInternedAttributeKey::OptimizeFor:
            BuildYsonFluently(consumer)
                .Value(table->GetOptimizeFor());
            return true;

        case EInternedAttributeKey::InMemoryMode:
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetInMemoryMode());
            return true;

        case EInternedAttributeKey::UpstreamReplicaId:
            if (!isDynamic) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->GetUpstreamReplicaId());
            return true;

        case EInternedAttributeKey::EnableTabletBalancer:
            if (!static_cast<bool>(trunkTable->GetEnableTabletBalancer())) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(*trunkTable->GetEnableTabletBalancer());
            return true;

        case EInternedAttributeKey::DisableTabletBalancer:
            if (!static_cast<bool>(trunkTable->GetEnableTabletBalancer())) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(!*trunkTable->GetEnableTabletBalancer());
            return true;

        case EInternedAttributeKey::MinTabletSize:
            if (!static_cast<bool>(trunkTable->GetMinTabletSize())) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(*trunkTable->GetMinTabletSize());
            return true;

        case EInternedAttributeKey::MaxTabletSize:
            if (!static_cast<bool>(trunkTable->GetMaxTabletSize())) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(*trunkTable->GetMaxTabletSize());
            return true;

        case EInternedAttributeKey::DesiredTabletSize:
            if (!static_cast<bool>(trunkTable->GetDesiredTabletSize())) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(*trunkTable->GetDesiredTabletSize());
            return true;

        case EInternedAttributeKey::DesiredTabletCount:
            if (!static_cast<bool>(trunkTable->GetDesiredTabletCount())) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(*trunkTable->GetDesiredTabletCount());
            return true;

        case EInternedAttributeKey::ForcedCompactionRevision:
            if (!trunkTable->GetForcedCompactionRevision()) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(*trunkTable->GetForcedCompactionRevision());
            return true;

        case EInternedAttributeKey::FlushLagTime: {
            if (!isSorted || !isDynamic || isExternal) {
                break;
            }
            auto unflushedTimestamp = table->GetCurrentUnflushedTimestamp(
                timestampProvider->GetLatestTimestamp());
            auto lastCommitTimestamp = trunkTable->GetLastCommitTimestamp();

            // NB: Proper order is not guaranteed.
            auto duration = TDuration::Zero();
            if (unflushedTimestamp <= lastCommitTimestamp) {
                duration = NTransactionClient::TimestampDiffToDuration(
                    unflushedTimestamp,
                    lastCommitTimestamp)
                    .second;
            }

            BuildYsonFluently(consumer)
                .Value(duration);
            return true;
        }

        case EInternedAttributeKey::TabletBalancerConfig:
            if (!isDynamic) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(trunkTable->TabletBalancerConfig());
            return true;

        default:
            break;
    }

    return TBase::GetBuiltinAttribute(key, consumer);
}

TFuture<TYsonString> TTableNodeProxy::GetBuiltinAttributeAsync(TInternedAttributeKey key)
{
    const auto* table = GetThisImpl();
    auto* chunkList = table->GetChunkList();
    auto isExternal = table->IsExternal();

    if (!isExternal) {
        switch (key) {
            case EInternedAttributeKey::TableChunkFormatStatistics:
                return ComputeChunkStatistics(
                    Bootstrap_,
                    chunkList,
                    [] (const TChunk* chunk) { return ETableChunkFormat(chunk->ChunkMeta().version()); });

            case EInternedAttributeKey::OptimizeForStatistics: {
                auto optimizeForExtractor = [] (const TChunk* chunk) {
                    switch (static_cast<ETableChunkFormat>(chunk->ChunkMeta().version())) {
                        case ETableChunkFormat::Old:
                        case ETableChunkFormat::VersionedSimple:
                        case ETableChunkFormat::Schemaful:
                        case ETableChunkFormat::SchemalessHorizontal:
                            return NTableClient::EOptimizeFor::Lookup;
                        case ETableChunkFormat::VersionedColumnar:
                        case ETableChunkFormat::UnversionedColumnar:
                            return NTableClient::EOptimizeFor::Scan;
                        default:
                            YT_ABORT();
                    }
                };

                return ComputeChunkStatistics(Bootstrap_, chunkList, optimizeForExtractor);
            }

            default:
                break;
        }
    }

    return TBase::GetBuiltinAttributeAsync(key);
}

bool TTableNodeProxy::RemoveBuiltinAttribute(TInternedAttributeKey key)
{
    switch (key) {
        case EInternedAttributeKey::EnableTabletBalancer: {
            ValidateNoTransaction();
            auto* lockedTable = LockThisImpl();
            lockedTable->SetEnableTabletBalancer(std::nullopt);
            return true;
        }

        case EInternedAttributeKey::DisableTabletBalancer: {
            ValidateNoTransaction();
            auto* lockedTable = LockThisImpl();
            lockedTable->SetEnableTabletBalancer(std::nullopt);
            return true;
        }

        case EInternedAttributeKey::MinTabletSize: {
            ValidateNoTransaction();
            auto* lockedTable = LockThisImpl();
            lockedTable->SetMinTabletSize(std::nullopt);
            return true;
        }

        case EInternedAttributeKey::MaxTabletSize: {
            ValidateNoTransaction();
            auto* lockedTable = LockThisImpl();
            lockedTable->SetMaxTabletSize(std::nullopt);
            return true;
        }

        case EInternedAttributeKey::DesiredTabletSize: {
            ValidateNoTransaction();
            auto* lockedTable = LockThisImpl();
            lockedTable->SetDesiredTabletSize(std::nullopt);
            return true;
        }

        case EInternedAttributeKey::DesiredTabletCount: {
            ValidateNoTransaction();
            auto* lockedTable = LockThisImpl();
            lockedTable->SetDesiredTabletCount(std::nullopt);
            return true;
        }

        case EInternedAttributeKey::ForcedCompactionRevision: {
            auto* lockedTable = LockThisImpl();
            lockedTable->SetForcedCompactionRevision(std::nullopt);
            return true;
        }

        default:
            break;
    }

    return TBase::RemoveBuiltinAttribute(key);
}

bool TTableNodeProxy::SetBuiltinAttribute(TInternedAttributeKey key, const TYsonString& value)
{
    const auto* table = GetThisImpl();

    switch (key) {
        case EInternedAttributeKey::TabletCellBundle: {
            ValidateNoTransaction();

            auto name = ConvertTo<TString>(value);
            const auto& tabletManager = Bootstrap_->GetTabletManager();
            auto* cellBundle = tabletManager->GetTabletCellBundleByNameOrThrow(name);
            cellBundle->ValidateCreationCommitted();

            auto* lockedTable = LockThisImpl();
            tabletManager->SetTabletCellBundle(lockedTable, cellBundle);

            return true;
        }

        case EInternedAttributeKey::Atomicity: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->ValidateAllTabletsUnmounted("Cannot change table atomicity mode");

            auto atomicity = ConvertTo<NTransactionClient::EAtomicity>(value);
            lockedTable->SetAtomicity(atomicity);

            return true;
        }

        case EInternedAttributeKey::CommitOrdering: {
            if (table->IsSorted()) {
                break;
            }
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->ValidateAllTabletsUnmounted("Cannot change table commit ordering mode");

            auto ordering = ConvertTo<NTransactionClient::ECommitOrdering>(value);
            lockedTable->SetCommitOrdering(ordering);

            return true;
        }

        case EInternedAttributeKey::OptimizeFor: {
            ValidatePermission(EPermissionCheckScope::This, EPermission::Write);

            const auto& uninternedKey = GetUninternedAttributeKey(key);
            auto* lockedTable = LockThisImpl<TTableNode>(TLockRequest::MakeSharedAttribute(uninternedKey));
            lockedTable->SetOptimizeFor(ConvertTo<EOptimizeFor>(value));

            return true;
        }

        case EInternedAttributeKey::InMemoryMode: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->ValidateAllTabletsUnmounted("Cannot change table memory mode");

            auto inMemoryMode = ConvertTo<EInMemoryMode>(value);
            lockedTable->SetInMemoryMode(inMemoryMode);

            return true;
        }

        case EInternedAttributeKey::EnableTabletBalancer: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->SetEnableTabletBalancer(ConvertTo<bool>(value));
            return true;
        }

        case EInternedAttributeKey::DisableTabletBalancer: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->SetEnableTabletBalancer(!ConvertTo<bool>(value));
            return true;
        }

        case EInternedAttributeKey::MinTabletSize: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->SetMinTabletSize(ConvertTo<i64>(value));
            return true;
        }

        case EInternedAttributeKey::MaxTabletSize: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->SetMaxTabletSize(ConvertTo<i64>(value));
            return true;
        }

        case EInternedAttributeKey::DesiredTabletSize: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->SetDesiredTabletSize(ConvertTo<i64>(value));
            return true;
        }

        case EInternedAttributeKey::DesiredTabletCount: {
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->SetDesiredTabletCount(ConvertTo<int>(value));
            return true;
        }

        case EInternedAttributeKey::ForcedCompactionRevision: {
            auto* lockedTable = LockThisImpl();
            const auto& hydraManager = Bootstrap_->GetHydraFacade()->GetHydraManager();
            auto revision = hydraManager->GetAutomatonVersion().ToRevision();
            lockedTable->SetForcedCompactionRevision(revision);
            return true;
        }

        case EInternedAttributeKey::TabletBalancerConfig: {
            if (!table->IsDynamic()) {
                break;
            }
            ValidateNoTransaction();

            auto* lockedTable = LockThisImpl();
            lockedTable->MutableTabletBalancerConfig() = ConvertTo<TTabletBalancerConfigPtr>(value);
            return true;
        }

        default:
            break;
    }

    return TBase::SetBuiltinAttribute(key, value);
}

void TTableNodeProxy::ValidateCustomAttributeUpdate(
    const TString& key,
    const TYsonString& oldValue,
    const TYsonString& newValue)
{
    auto internedKey = GetInternedAttributeKey(key);

    switch (internedKey) {
        case EInternedAttributeKey::ChunkWriter:
            if (!newValue) {
                break;
            }
            ConvertTo<NTabletNode::TTabletChunkWriterConfigPtr>(newValue);
            return;

        case EInternedAttributeKey::ChunkReader:
            if (!newValue) {
                break;
            }
            ConvertTo<NTabletNode::TTabletChunkReaderConfigPtr>(newValue);
            return;

        default:
            break;
    }

    TBase::ValidateCustomAttributeUpdate(key, oldValue, newValue);
}

void TTableNodeProxy::ValidateFetch(TFetchContext* context)
{
    TChunkOwnerNodeProxy::ValidateFetch(context);

    auto* table = GetThisImpl();
    for (const auto& range : context->Ranges) {
        const auto& lowerLimit = range.LowerLimit();
        const auto& upperLimit = range.UpperLimit();
        if ((upperLimit.HasKey() || lowerLimit.HasKey()) && !table->IsSorted()) {
            THROW_ERROR_EXCEPTION("Key selectors are not supported for unsorted tables");
        }
        if ((upperLimit.HasRowIndex() || lowerLimit.HasRowIndex()) && table->IsDynamic()) {
            THROW_ERROR_EXCEPTION("Row index selectors are not supported for dynamic tables");
        }
        if (upperLimit.HasOffset() || lowerLimit.HasOffset()) {
            THROW_ERROR_EXCEPTION("Offset selectors are not supported for tables");
        }
    }
}

bool TTableNodeProxy::DoInvoke(const IServiceContextPtr& context)
{
    DISPATCH_YPATH_SERVICE_METHOD(Mount);
    DISPATCH_YPATH_SERVICE_METHOD(Unmount);
    DISPATCH_YPATH_SERVICE_METHOD(Remount);
    DISPATCH_YPATH_SERVICE_METHOD(Freeze);
    DISPATCH_YPATH_SERVICE_METHOD(Unfreeze);
    DISPATCH_YPATH_SERVICE_METHOD(Reshard);
    DISPATCH_YPATH_SERVICE_METHOD(ReshardAutomatic);
    DISPATCH_YPATH_SERVICE_METHOD(GetMountInfo);
    DISPATCH_YPATH_SERVICE_METHOD(Alter);
    DISPATCH_YPATH_SERVICE_METHOD(LockDynamicTable);
    DISPATCH_YPATH_SERVICE_METHOD(CheckDynamicTableLock);
    return TBase::DoInvoke(context);
}

void TTableNodeProxy::ValidateBeginUpload()
{
    TBase::ValidateBeginUpload();
    const auto* table = GetThisImpl();

    if (table->IsDynamic() && !table->GetTableSchema().IsSorted()) {
        THROW_ERROR_EXCEPTION("Cannot upload into ordered dynamic table");
    }

    if (table->IsDynamic() && !Bootstrap_->GetConfigManager()->GetConfig()->TabletManager->EnableBulkInsert) {
        THROW_ERROR_EXCEPTION("Bulk insert is disabled");
    }
}

void TTableNodeProxy::ValidateStorageParametersUpdate()
{
    TChunkOwnerNodeProxy::ValidateStorageParametersUpdate();

    const auto* table = GetThisImpl();
    table->ValidateAllTabletsUnmounted("Cannot change storage parameters");
}

void TTableNodeProxy::ValidateLockPossible()
{
    TChunkOwnerNodeProxy::ValidateLockPossible();

    const auto* table = GetThisImpl();
    table->ValidateTabletStateFixed("Cannot lock table");
}

void TTableNodeProxy::CallViaNativeClient(const TString& user, std::function<TFuture<void>(const IClientPtr&)> callback)
{
    const auto& connection = Bootstrap_->GetClusterConnection();
    auto asyncPair = BIND([&] {
            auto client = connection->CreateNativeClient(TClientOptions(user));
            return std::make_pair(client, callback(client));
        })
        .AsyncVia(NRpc::TDispatcher::Get()->GetHeavyInvoker())
        .Run();

    auto pair = WaitFor(asyncPair)
        .ValueOrThrow();
    WaitFor(pair.second)
        .ThrowOnError();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, Mount)
{
    DeclareNonMutating();

    int firstTabletIndex = request->first_tablet_index();
    int lastTabletIndex = request->last_tablet_index();
    auto cellId = FromProto<TTabletCellId>(request->cell_id());
    auto targetCellIds = FromProto<std::vector<TTabletCellId>>(request->target_cell_ids());
    bool freeze = request->freeze();

    context->SetRequestInfo(
        "FirstTabletIndex: %v, LastTabletIndex: %v, CellId: %v, Freeze: %v",
        firstTabletIndex,
        lastTabletIndex,
        cellId,
        freeze);

    const auto& cypressManager = Bootstrap_->GetCypressManager();
    auto path = cypressManager->GetNodePath(this);

    TMountTableOptions options;
    options.FirstTabletIndex = firstTabletIndex;
    options.LastTabletIndex = lastTabletIndex;
    options.CellId = cellId;
    options.TargetCellIds = targetCellIds;
    options.Freeze = freeze;

    CallViaNativeClient(context->GetUser(), [=] (const IClientPtr& client) {
        return client->MountTable(path, options);
    });

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, Unmount)
{
    DeclareNonMutating();

    int firstTabletIndex = request->first_tablet_index();
    int lastTabletIndex = request->last_tablet_index();
    bool force = request->force();

    context->SetRequestInfo("FirstTabletIndex: %v, LastTabletIndex: %v, Force: %v",
        firstTabletIndex,
        lastTabletIndex,
        force);

    const auto& cypressManager = Bootstrap_->GetCypressManager();
    auto path = cypressManager->GetNodePath(this);

    TUnmountTableOptions options;
    options.FirstTabletIndex = firstTabletIndex;
    options.LastTabletIndex = lastTabletIndex;
    options.Force = force;

    CallViaNativeClient(context->GetUser(), [=] (const IClientPtr& client) {
        return client->UnmountTable(path, options);
    });

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, Freeze)
{
    DeclareNonMutating();

    int firstTabletIndex = request->first_tablet_index();
    int lastTabletIndex = request->last_tablet_index();

    context->SetRequestInfo(
        "FirstTabletIndex: %v, LastTabletIndex: %v",
        firstTabletIndex,
        lastTabletIndex);

    const auto& cypressManager = Bootstrap_->GetCypressManager();
    auto path = cypressManager->GetNodePath(this);

    TFreezeTableOptions options;
    options.FirstTabletIndex = firstTabletIndex;
    options.LastTabletIndex = lastTabletIndex;

    CallViaNativeClient(context->GetUser(), [=] (const IClientPtr& client) {
        return client->FreezeTable(path, options);
    });

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, Unfreeze)
{
    DeclareNonMutating();

    int firstTabletIndex = request->first_tablet_index();
    int lastTabletIndex = request->last_tablet_index();

    context->SetRequestInfo("FirstTabletIndex: %v, LastTabletIndex: %v",
        firstTabletIndex,
        lastTabletIndex);

    const auto& cypressManager = Bootstrap_->GetCypressManager();
    auto path = cypressManager->GetNodePath(this);

    TUnfreezeTableOptions options;
    options.FirstTabletIndex = firstTabletIndex;
    options.LastTabletIndex = lastTabletIndex;

    CallViaNativeClient(context->GetUser(), [=] (const IClientPtr& client) {
        return client->UnfreezeTable(path, options);
    });

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, Remount)
{
    DeclareNonMutating();

    int firstTabletIndex = request->first_tablet_index();
    int lastTabletIndex = request->first_tablet_index();

    context->SetRequestInfo("FirstTabletIndex: %v, LastTabletIndex: %v",
        firstTabletIndex,
        lastTabletIndex);

    const auto& cypressManager = Bootstrap_->GetCypressManager();
    auto path = cypressManager->GetNodePath(this);

    TRemountTableOptions options;
    options.FirstTabletIndex = firstTabletIndex;
    options.LastTabletIndex = lastTabletIndex;

    CallViaNativeClient(context->GetUser(), [=] (const IClientPtr& client) {
        return client->RemountTable(path, options);
    });

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, Reshard)
{
    DeclareNonMutating();

    int firstTabletIndex = request->first_tablet_index();
    int lastTabletIndex = request->last_tablet_index();
    int tabletCount = request->tablet_count();
    auto pivotKeys = FromProto<std::vector<TOwningKey>>(request->pivot_keys());

    context->SetRequestInfo("FirstTabletIndex: %v, LastTabletIndex: %v, TabletCount: %v",
        firstTabletIndex,
        lastTabletIndex,
        tabletCount);

    const auto& cypressManager = Bootstrap_->GetCypressManager();
    auto path = cypressManager->GetNodePath(this);

    TReshardTableOptions options;
    options.FirstTabletIndex = firstTabletIndex;
    options.LastTabletIndex = lastTabletIndex;

    CallViaNativeClient(context->GetUser(), [=] (const IClientPtr& client) {
        if (pivotKeys.empty()) {
            return client->ReshardTable(path, tabletCount, options);
        } else {
            return client->ReshardTable(path, pivotKeys, options);
        }
    });

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, ReshardAutomatic)
{
    DeclareMutating();

    bool keepActions = request->keep_actions();

    context->SetRequestInfo("KeepActions: %v", keepActions);

    ValidateNoTransaction();

    auto* trunkTable = GetThisImpl();

    const auto& tabletManager = Bootstrap_->GetTabletManager();
    auto tabletActions = tabletManager->SyncBalanceTablets(trunkTable, keepActions);
    ToProto(response->mutable_tablet_actions(), tabletActions);

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, GetMountInfo)
{
    DeclareNonMutating();
    SuppressAccessTracking();

    context->SetRequestInfo();

    ValidateNotExternal();
    ValidateNoTransaction();

    const auto* trunkTable = GetThisImpl();

    ToProto(response->mutable_table_id(), trunkTable->GetId());
    response->set_dynamic(trunkTable->IsDynamic());
    ToProto(response->mutable_upstream_replica_id(), trunkTable->GetUpstreamReplicaId());
    ToProto(response->mutable_schema(), trunkTable->GetTableSchema());

    THashSet<TTabletCell*> cells;
    for (auto* tablet : trunkTable->Tablets()) {
        auto* cell = tablet->GetCell();
        auto* protoTablet = response->add_tablets();
        ToProto(protoTablet->mutable_tablet_id(), tablet->GetId());
        protoTablet->set_mount_revision(tablet->GetMountRevision());
        protoTablet->set_state(static_cast<int>(tablet->GetState()));
        protoTablet->set_in_memory_mode(static_cast<int>(tablet->GetInMemoryMode()));
        ToProto(protoTablet->mutable_pivot_key(), tablet->GetPivotKey());
        if (cell) {
            ToProto(protoTablet->mutable_cell_id(), cell->GetId());
            cells.insert(cell);
        }
    }

    for (const auto* cell : cells) {
        ToProto(response->add_tablet_cells(), cell->GetDescriptor());
    }

    if (trunkTable->IsReplicated()) {
        const auto* replicatedTable = trunkTable->As<TReplicatedTableNode>();
        for (const auto* replica : replicatedTable->Replicas()) {
            auto* protoReplica = response->add_replicas();
            ToProto(protoReplica->mutable_replica_id(), replica->GetId());
            protoReplica->set_cluster_name(replica->GetClusterName());
            protoReplica->set_replica_path(replica->GetReplicaPath());
            protoReplica->set_mode(static_cast<int>(replica->GetMode()));
        }
    }

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, Alter)
{
    DeclareMutating();

    struct TAlterTableOptions
    {
        std::optional<NTableClient::TTableSchema> Schema;
        std::optional<bool> Dynamic;
        std::optional<NTabletClient::TTableReplicaId> UpstreamReplicaId;
    } options;

    if (request->has_schema()) {
        options.Schema = FromProto<TTableSchema>(request->schema());
    }
    if (request->has_dynamic()) {
        options.Dynamic = request->dynamic();
    }
    if (request->has_upstream_replica_id()) {
        options.UpstreamReplicaId = FromProto<TTableReplicaId>(request->upstream_replica_id());
    }

    context->SetRequestInfo("Schema: %v, Dynamic: %v, UpstreamReplicaId: %v",
        options.Schema,
        options.Dynamic,
        options.UpstreamReplicaId);

    const auto& tabletManager = Bootstrap_->GetTabletManager();
    auto* table = LockThisImpl();
    auto dynamic = options.Dynamic.value_or(table->IsDynamic());
    auto schema = options.Schema.value_or(table->GetTableSchema());

    // NB: Sorted dynamic tables contain unique keys, set this for user.
    if (dynamic && options.Schema && options.Schema->IsSorted() && !options.Schema->GetUniqueKeys()) {
        schema = schema.ToUniqueKeys();
    }

    if (table->IsNative()) {
        ValidatePermission(EPermissionCheckScope::This, EPermission::Write);

        if (table->IsReplicated()) {
            THROW_ERROR_EXCEPTION("Cannot alter a replicated table");
        }

        if (options.Dynamic) {
            ValidateNoTransaction();
        }

        if (options.Schema && table->IsDynamic()) {
            table->ValidateAllTabletsUnmounted("Cannot change table schema");
        }

        if (options.UpstreamReplicaId) {
            ValidateNoTransaction();

            if (!dynamic) {
                THROW_ERROR_EXCEPTION("Upstream replica can only be set for dynamic tables");
            }
            if (table->IsReplicated()) {
                THROW_ERROR_EXCEPTION("Upstream replica cannot be explicitly set for replicated tables");
            }

            table->ValidateAllTabletsUnmounted("Cannot change upstream replica");
        }

        ValidateTableSchemaUpdate(
            table->GetTableSchema(),
            schema,
            dynamic,
            table->IsEmpty() && !table->IsDynamic());

        if (options.Dynamic) {
            if (*options.Dynamic) {
                tabletManager->ValidateMakeTableDynamic(table);
            } else {
                tabletManager->ValidateMakeTableStatic(table);
            }
        }
    }

    if (options.Schema) {
        table->SharedTableSchema() = Bootstrap_->GetCypressManager()->GetSharedTableSchemaRegistry()->GetSchema(
            std::move(schema));
        table->SetSchemaMode(ETableSchemaMode::Strong);
    }

    if (options.Dynamic) {
        if (*options.Dynamic) {
            tabletManager->MakeTableDynamic(table);
        } else {
            tabletManager->MakeTableStatic(table);
        }
    }

    if (options.UpstreamReplicaId) {
        table->SetUpstreamReplicaId(*options.UpstreamReplicaId);
    }

    if (table->IsExternal()) {
        PostToMaster(context, table->GetExternalCellTag());
    }

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, LockDynamicTable)
{
    DeclareMutating();

    context->SetRequestInfo();

    const auto& tabletManager = Bootstrap_->GetTabletManager();
    tabletManager->LockDynamicTable(
        GetThisImpl()->GetTrunkNode(),
        GetTransaction(),
        request->timestamp());

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TTableNodeProxy, CheckDynamicTableLock)
{
    context->SetRequestInfo();

    const auto& tabletManager = Bootstrap_->GetTabletManager();
    tabletManager->CheckDynamicTableLock(
        GetThisImpl()->GetTrunkNode(),
        GetTransaction(),
        response);

    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

TReplicatedTableNodeProxy::TReplicatedTableNodeProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TTransaction* transaction,
    TReplicatedTableNode* trunkNode)
    : TTableNodeProxy(
        bootstrap,
        metadata,
        transaction,
        trunkNode)
{ }

void TReplicatedTableNodeProxy::ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors)
{
    TTableNodeProxy::ListSystemAttributes(descriptors);

    const auto* table = GetThisImpl();

    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Replicas)
        .SetExternal(table->IsExternal())
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ReplicatedTableOptions)
        .SetReplicated(true)
        .SetWritable(true));
}

bool TReplicatedTableNodeProxy::GetBuiltinAttribute(TInternedAttributeKey key, IYsonConsumer* consumer)
{
    const auto* table = GetThisImpl<TReplicatedTableNode>();
    const auto& timestampProvider = Bootstrap_->GetTimestampProvider();
    auto isExternal = table->IsExternal();

    switch (key) {
        case EInternedAttributeKey::Replicas: {
            if (isExternal) {
                break;
            }

            const auto& objectManager = Bootstrap_->GetObjectManager();
            BuildYsonFluently(consumer)
                .DoMapFor(table->Replicas(), [&] (TFluentMap fluent, TTableReplica* replica) {
                    auto replicaProxy = objectManager->GetProxy(replica);
                    fluent
                        .Item(ToString(replica->GetId()))
                        .BeginMap()
                            .Item("cluster_name").Value(replica->GetClusterName())
                            .Item("replica_path").Value(replica->GetReplicaPath())
                            .Item("state").Value(replica->GetState())
                            .Item("mode").Value(replica->GetMode())
                            .Item("replication_lag_time").Value(replica->ComputeReplicationLagTime(
                                timestampProvider->GetLatestTimestamp()))
                            .Item("errors").Value(replica->GetErrors(ReplicationErrorCountViewLimit))
                        .EndMap();
                });
            return true;
        }

        case EInternedAttributeKey::ReplicatedTableOptions: {
            BuildYsonFluently(consumer)
                .Value(table->GetReplicatedTableOptions());
            return true;
        }

        default:
            break;
    }

    return TTableNodeProxy::GetBuiltinAttribute(key, consumer);
}

bool TReplicatedTableNodeProxy::SetBuiltinAttribute(TInternedAttributeKey key, const TYsonString& value)
{
    auto* table = GetThisImpl<TReplicatedTableNode>();

    switch (key) {
        case EInternedAttributeKey::ReplicatedTableOptions: {
            auto options = ConvertTo<TReplicatedTableOptionsPtr>(value);
            table->SetReplicatedTableOptions(options);
            return true;
        }

        default:
            break;
    }

    return TTableNodeProxy::SetBuiltinAttribute(key, value);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableServer


