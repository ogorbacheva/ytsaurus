#include "query_executor.h"
#include "query_service.h"
#include "public.h"
#include "private.h"

#include <yt/server/node/cluster_node/bootstrap.h>

#include <yt/server/node/data_node/master_connector.h>

#include <yt/server/node/query_agent/config.h>

#include <yt/server/node/tablet_node/security_manager.h>
#include <yt/server/node/tablet_node/slot_manager.h>
#include <yt/server/node/tablet_node/store.h>
#include <yt/server/node/tablet_node/tablet.h>
#include <yt/server/node/tablet_node/tablet_reader.h>
#include <yt/server/node/tablet_node/tablet_slot.h>
#include <yt/server/node/tablet_node/tablet_manager.h>
#include <yt/server/node/tablet_node/lookup.h>
#include <yt/server/node/tablet_node/transaction_manager.h>

#include <yt/server/lib/misc/profiling_helpers.h>

#include <yt/server/lib/tablet_node/config.h>

#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/ytlib/chunk_client/chunk_reader_statistics.h>

#include <yt/client/node_tracker_client/node_directory.h>

#include <yt/ytlib/query_client/query.h>
#include <yt/ytlib/query_client/query_service_proxy.h>
#include <yt/ytlib/query_client/functions_cache.h>

#include <yt/client/query_client/query_statistics.h>

#include <yt/client/table_client/helpers.h>
#include <yt/client/table_client/unversioned_writer.h>
#include <yt/client/table_client/versioned_reader.h>
#include <yt/client/table_client/wire_protocol.h>

#include <yt/core/compression/codec.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/misc/finally.h>
#include <yt/core/misc/protobuf_helpers.h>
#include <yt/core/misc/tls_cache.h>

#include <yt/core/profiling/profile_manager.h>

#include <yt/core/rpc/service_detail.h>
#include <yt/core/rpc/authentication_identity.h>

namespace NYT::NQueryAgent {

using namespace NClusterNode;
using namespace NChunkClient;
using namespace NCompression;
using namespace NConcurrency;
using namespace NHydra;
using namespace NProfiling;
using namespace NQueryClient;
using namespace NRpc;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTabletNode;
using namespace NYTree;
using namespace NYson;

using NChunkClient::NProto::TMiscExt;

////////////////////////////////////////////////////////////////////////////////

static const auto& Profiler = QueryAgentProfiler;

////////////////////////////////////////////////////////////////////////////////

// TODO(ifsmirnov): YT_12491 - move this to reader config and dynamically choose
// row count based on desired streaming window data size.
static constexpr size_t MaxRowsPerRemoteDynamicStoreRead = 1024;

////////////////////////////////////////////////////////////////////////////////

bool IsRetriableError(const TError& error)
{
    return
        error.FindMatching(NDataNode::EErrorCode::LocalChunkReaderFailed) ||
        error.FindMatching(NChunkClient::EErrorCode::NoSuchChunk) ||
        error.FindMatching(NTabletClient::EErrorCode::TabletSnapshotExpired);
}

template <class T>
T ExecuteRequestWithRetries(
    int maxRetries,
    const NLogging::TLogger& logger,
    const std::function<T()>& callback)
{
    const auto& Logger = logger;
    std::vector<TError> errors;
    for (int retryIndex = 0; retryIndex < maxRetries; ++retryIndex) {
        try {
            return callback();
        } catch (const std::exception& ex) {
            auto error = TError(ex);
            if (IsRetriableError(error)) {
                YT_LOG_INFO(error, "Request failed, retrying");
                errors.push_back(error);
                continue;
            } else {
                throw;
            }
        }
    }
    THROW_ERROR_EXCEPTION("Request failed after %v retries", maxRetries)
        << errors;
}

void ValidateColumnFilterContainsAllKeyColumns(
    const TColumnFilter& columnFilter,
    const TTableSchema& schema)
{
    if (columnFilter.IsUniversal()) {
        return;
    }

    for (int columnIndex = 0; columnIndex < schema.GetKeyColumnCount(); ++columnIndex) {
        if (!columnFilter.ContainsIndex(columnIndex)) {
            THROW_ERROR_EXCEPTION("Column filter does not contain key column %Qv with index %v",
                schema.Columns()[columnIndex].Name(),
                columnIndex);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

class TQueryService
    : public TServiceBase
{
public:
    TQueryService(
        TQueryAgentConfigPtr config,
        NClusterNode::TBootstrap* bootstrap)
        : TServiceBase(
            bootstrap->GetQueryPoolInvoker({}, 1.0, {}),
            TQueryServiceProxy::GetDescriptor(),
            QueryAgentLogger)
        , Config_(config)
        , Bootstrap_(bootstrap)
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(Execute)
            .SetCancelable(true));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(Multiread)
            .SetCancelable(true)
            .SetInvoker(bootstrap->GetTabletLookupPoolInvoker()));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GetTabletInfo)
            .SetInvoker(bootstrap->GetTabletLookupPoolInvoker()));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ReadDynamicStore)
            .SetCancelable(true)
            .SetStreamingEnabled(true)
            .SetResponseCodec(NCompression::ECodec::Lz4));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(FetchTabletStores)
            .SetInvoker(bootstrap->GetStorageHeavyInvoker()));
    }

private:
    const TQueryAgentConfigPtr Config_;
    NClusterNode::TBootstrap* const Bootstrap_;


    DECLARE_RPC_SERVICE_METHOD(NQueryClient::NProto, Execute)
    {
        TServiceProfilerGuard profilerGuard(&Profiler, "/execute");

        YT_LOG_DEBUG("Deserializing subfragment");

        auto query = FromProto<TConstQueryPtr>(request->query());
        context->SetRequestInfo("FragmentId: %v", query->Id);

        auto externalCGInfo = New<TExternalCGInfo>();
        FromProto(&externalCGInfo->Functions, request->external_functions());
        externalCGInfo->NodeDirectory->MergeFrom(request->node_directory());

        auto options = FromProto<TQueryOptions>(request->options());
        options.InputRowLimit = request->query().input_row_limit();
        options.OutputRowLimit = request->query().output_row_limit();

        auto dataSources = FromProto<std::vector<TDataRanges>>(request->data_sources());

        YT_LOG_DEBUG("Deserialized subfragment (FragmentId: %v, InputRowLimit: %v, OutputRowLimit: %v, "
            "RangeExpansionLimit: %v, MaxSubqueries: %v, EnableCodeCache: %v, WorkloadDescriptor: %v, "
            "ReadSesisonId: %v, MemoryLimitPerNode: %v, DataRangeCount: %v)",
            query->Id,
            options.InputRowLimit,
            options.OutputRowLimit,
            options.RangeExpansionLimit,
            options.MaxSubqueries,
            options.EnableCodeCache,
            options.WorkloadDescriptor,
            options.ReadSessionId,
            options.MemoryLimitPerNode,
            dataSources.size());

        TClientBlockReadOptions blockReadOptions;
        blockReadOptions.WorkloadDescriptor = options.WorkloadDescriptor;
        blockReadOptions.ChunkReaderStatistics = New<TChunkReaderStatistics>();
        blockReadOptions.ReadSessionId = options.ReadSessionId;

        ExecuteRequestWithRetries<void>(
            Config_->MaxQueryRetries,
            Logger,
            [&] {
                auto codecId = CheckedEnumCast<ECodec>(request->response_codec());
                auto writer = CreateWireProtocolRowsetWriter(
                    codecId,
                    Config_->DesiredUncompressedResponseBlockSize,
                    query->GetTableSchema(),
                    false,
                    Logger);

                const auto& executor = Bootstrap_->GetQueryExecutor();
                auto asyncResult = executor->Execute(
                    query,
                    externalCGInfo,
                    dataSources,
                    writer,
                    blockReadOptions,
                    options,
                    profilerGuard);
                auto result = WaitFor(asyncResult)
                    .ValueOrThrow();

                response->Attachments() = writer->GetCompressedBlocks();
                ToProto(response->mutable_query_statistics(), result);
                context->Reply();
            });
    }

    DECLARE_RPC_SERVICE_METHOD(NQueryClient::NProto, Multiread)
    {
        TServiceProfilerGuard profilerGuard(&Profiler, "/multiread");

        auto requestCodecId = CheckedEnumCast<NCompression::ECodec>(request->request_codec());
        auto responseCodecId = CheckedEnumCast<NCompression::ECodec>(request->response_codec());
        auto timestamp = TTimestamp(request->timestamp());
        // TODO(sandello): Extract this out of RPC request.
        TClientBlockReadOptions blockReadOptions;
        blockReadOptions.WorkloadDescriptor = TWorkloadDescriptor(EWorkloadCategory::UserInteractive);
        blockReadOptions.ChunkReaderStatistics = New<TChunkReaderStatistics>();
        blockReadOptions.ReadSessionId = TReadSessionId::Create();

        TRetentionConfigPtr retentionConfig;
        if (request->has_retention_config()) {
            retentionConfig = ConvertTo<TRetentionConfigPtr>(TYsonString(request->retention_config()));
        }

        const auto& slotManager = Bootstrap_->GetTabletSlotManager();

        size_t batchCount = request->tablet_ids_size();
        YT_VERIFY(batchCount == request->mount_revisions_size());
        YT_VERIFY(batchCount == request->Attachments().size());

        auto tabletIds = FromProto<std::vector<TTabletId>>(request->tablet_ids());

        context->SetRequestInfo("TabletIds: %v, Timestamp: %llx, RequestCodec: %v, ResponseCodec: %v, ReadSessionId: %v, RetentionConfig: %v",
            tabletIds,
            timestamp,
            requestCodecId,
            responseCodecId,
            blockReadOptions.ReadSessionId,
            retentionConfig);

        auto* requestCodec = NCompression::GetCodec(requestCodecId);
        auto* responseCodec = NCompression::GetCodec(responseCodecId);

        bool useLookupCache = request->use_lookup_cache();

        std::vector<TCallback<TFuture<TSharedRef>()>> batchCallbacks;
        for (size_t index = 0; index < batchCount; ++index) {
            auto tabletId = tabletIds[index];
            auto mountRevision = request->mount_revisions(index);
            auto attachment = request->Attachments()[index];

            if (auto tabletSnapshot = slotManager->FindTabletSnapshot(tabletId, mountRevision)) {
                // TODO(prime@):
                // if (tabletSnapshot->IsProfilingEnabled() && profilerGuard.GetProfilerTags().empty()) {
                //     profilerGuard.SetProfilerTags(AddCurrentUserTag(tabletSnapshot->ProfilerTags));
                // }
            }

            auto callback = BIND([=, identity = NRpc::GetCurrentAuthenticationIdentity()] {
                try {
                    return ExecuteRequestWithRetries<TSharedRef>(
                        Config_->MaxQueryRetries,
                        Logger,
                        [&] {
                            TCurrentAuthenticationIdentityGuard identityGuard(&identity);

                            auto tabletSnapshot = slotManager->GetTabletSnapshotOrThrow(tabletId, mountRevision);

                            slotManager->ValidateTabletAccess(tabletSnapshot, timestamp);

                            auto requestData = requestCodec->Decompress(attachment);

                            struct TLookupRowBufferTag { };
                            TWireProtocolReader reader(requestData, New<TRowBuffer>(TLookupRowBufferTag()));
                            TWireProtocolWriter writer;

                            LookupRead(
                                tabletSnapshot,
                                timestamp,
                                useLookupCache,
                                blockReadOptions,
                                retentionConfig,
                                &reader,
                                &writer);

                            return responseCodec->Compress(writer.Finish());
                        });
                } catch (const TErrorException&) {
                    if (auto tabletSnapshot = slotManager->FindLatestTabletSnapshot(tabletId)) {
                        ++tabletSnapshot->PerformanceCounters->LookupErrorCount;
                    }

                    throw;
                }
            }).AsyncVia(Bootstrap_->GetTabletLookupPoolInvoker());

            batchCallbacks.push_back(callback);
        }

        auto results = WaitFor(RunWithBoundedConcurrency(batchCallbacks, Config_->MaxSubqueries))
            .ValueOrThrow();

        for (const auto& result : results) {
            if (request->enable_partial_result() && !result.IsOK()) {
                response->Attachments().emplace_back();
                continue;
            }

            response->Attachments().push_back(result.ValueOrThrow());
        }

        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NQueryClient::NProto, GetTabletInfo)
    {
        auto tabletIds = FromProto<std::vector<TTabletId>>(request->tablet_ids());

        context->SetRequestInfo("TabletIds: %v", tabletIds);

        const auto& slotManager = Bootstrap_->GetTabletSlotManager();

        for (auto tabletId : tabletIds) {
            auto tabletSnapshot = slotManager->GetLatestTabletSnapshotOrThrow(tabletId);

            auto* protoTabletInfo = response->add_tablets();
            ToProto(protoTabletInfo->mutable_tablet_id(), tabletId);
            // NB: Read barrier timestamp first to ensure a certain degree of consistency with TotalRowCount.
            protoTabletInfo->set_barrier_timestamp(tabletSnapshot->TabletCellRuntimeData->BarrierTimestamp.load());
            protoTabletInfo->set_total_row_count(tabletSnapshot->TabletRuntimeData->TotalRowCount.load());
            protoTabletInfo->set_trimmed_row_count(tabletSnapshot->TabletRuntimeData->TrimmedRowCount.load());
            protoTabletInfo->set_last_write_timestamp(tabletSnapshot->TabletRuntimeData->LastWriteTimestamp.load());

            for (const auto& [replicaId, replicaSnapshot] : tabletSnapshot->Replicas) {
                auto lastReplicationTimestamp = replicaSnapshot->RuntimeData->LastReplicationTimestamp.load();
                if (lastReplicationTimestamp == NullTimestamp) {
                    lastReplicationTimestamp = replicaSnapshot->RuntimeData->CurrentReplicationTimestamp.load();
                }

                auto* protoReplicaInfo = protoTabletInfo->add_replicas();
                ToProto(protoReplicaInfo->mutable_replica_id(), replicaId);
                protoReplicaInfo->set_last_replication_timestamp(lastReplicationTimestamp);
                protoReplicaInfo->set_mode(static_cast<int>(replicaSnapshot->RuntimeData->Mode.load()));
                protoReplicaInfo->set_current_replication_row_index(replicaSnapshot->RuntimeData->CurrentReplicationRowIndex.load());
            }
        }
        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NQueryClient::NProto, ReadDynamicStore)
    {
        auto storeId = FromProto<TDynamicStoreId>(request->store_id());
        auto tabletId = FromProto<TTabletId>(request->tablet_id());
        auto readSessionId = FromProto<TReadSessionId>(request->read_session_id());

        context->SetRequestInfo("StoreId: %v, TabletId: %v, ReadSessionId: %v, Timestamp: %llx",
            storeId,
            tabletId,
            readSessionId,
            request->timestamp());

        const auto& slotManager = Bootstrap_->GetTabletSlotManager();
        auto tabletSnapshot = slotManager->GetLatestTabletSnapshotOrThrow(tabletId);

        if (tabletSnapshot->IsPreallocatedDynamicStoreId(storeId)) {
            YT_LOG_DEBUG("Dynamic store is not created yet, sending nothing (TabletId: %v, StoreId: %v, "
                "ReadSessionId: %v, RequestId: %v)",
                tabletId,
                storeId,
                readSessionId,
                context->GetRequestId());
            HandleInputStreamingRequest(
                context,
                [] { return TSharedRef(); });
            return;
        }

        auto profilingCounters = tabletSnapshot->TableProfiler->GetRemoteDynamicStoreReadCounters(GetCurrentProfilingUser());

        TWallTimer wallTimer;
        i64 sessionRowCount = 0;
        i64 sessionDataWeight = 0;

        auto dynamicStore = tabletSnapshot->GetDynamicStoreOrThrow(storeId);

        TColumnFilter columnFilter;
        if (request->has_column_filter()) {
            columnFilter = TColumnFilter(FromProto<TColumnFilter::TIndexes>(request->column_filter().indexes()));
            ValidateColumnFilter(columnFilter, tabletSnapshot->PhysicalSchema->GetColumnCount());
            ValidateColumnFilterContainsAllKeyColumns(columnFilter, *tabletSnapshot->PhysicalSchema);
        }

        auto bandwidthThrottler = Bootstrap_->GetTabletNodeOutThrottler(EWorkloadCategory::UserDynamicStoreRead);

        bool sorted = tabletSnapshot->PhysicalSchema->IsSorted();

        if (sorted) {
            auto lowerBound = request->has_lower_bound()
                ? FromProto<TLegacyOwningKey>(request->lower_bound())
                : MinKey();
            auto upperBound = request->has_upper_bound()
                ? FromProto<TLegacyOwningKey>(request->upper_bound())
                : MaxKey();
            TTimestamp timestamp = request->timestamp();

            // NB: Options and throttler are not used by the reader.
            auto reader = dynamicStore->AsSorted()->CreateReader(
                tabletSnapshot,
                MakeSingletonRowRange(lowerBound, upperBound),
                timestamp,
                /*produceAllVersions*/ false,
                columnFilter,
                TClientBlockReadOptions(),
                GetUnlimitedThrottler());
            WaitFor(reader->Open())
                .ThrowOnError();

            std::vector<TVersionedRow> rows;
            rows.reserve(MaxRowsPerRemoteDynamicStoreRead);

            TWireProtocolWriter writer;
            writer.WriteVersionedRowset(rows);

            YT_LOG_DEBUG("Started serving remote dynamic store read request "
                "(TabletId: %v, StoreId: %v, Timestamp: %v, ReadSessionId: %v, "
                "LowerBound: %v, UpperBound: %v, ColumnFilter: %v, RequestId: %v)",
                tabletId,
                storeId,
                timestamp,
                readSessionId,
                lowerBound,
                upperBound,
                columnFilter,
                context->GetRequestId());

            HandleInputStreamingRequest(context, [&] {
                TFiberWallTimer timer;
                i64 rowCount = 0;
                i64 dataWeight = 0;
                auto finallyGuard = Finally([&] {
                    profilingCounters->RowCount.Increment(rowCount);
                    profilingCounters->DataWeight.Increment(dataWeight);
                    profilingCounters->CpuTime.Add(timer.GetElapsedTime());

                    sessionRowCount += rowCount;
                    sessionDataWeight += dataWeight;
                });

                // NB: Dynamic store reader is non-blocking in the sense of ready event.
                // However, waiting on blocked row may occur. See YT-12492.
                reader->Read(&rows);
                if (rows.empty()) {
                    return TSharedRef();
                }
                rowCount += rows.size();

                TWireProtocolWriter writer;
                writer.WriteVersionedRowset(rows);
                auto data = writer.Finish();

                struct TReadDynamicStoreTag { };
                auto mergedRef = MergeRefsToRef<TReadDynamicStoreTag>(data);
                dataWeight += mergedRef.size();

                auto throttleResult = WaitFor(bandwidthThrottler->Throttle(mergedRef.size()));
                THROW_ERROR_EXCEPTION_IF_FAILED(throttleResult, "Failed to throttle out bandwidth in dynamic store reader");

                return mergedRef;
            });

            profilingCounters->SessionRowCount.Record(sessionRowCount);
            profilingCounters->SessionDataWeight.Record(sessionDataWeight);
            profilingCounters->SessionWallTime.Record(wallTimer.GetElapsedTime());
        } else {
            THROW_ERROR_EXCEPTION("Remote reader for ordered dynamic stores is not implemented");
        }
    }

    void BuildChunkSpec(
        const IChunkStorePtr& chunk,
        TReadLimit lowerLimit,
        TReadLimit upperLimit,
        bool fetchAllMetaExtensions,
        const THashSet<int>& extensionTags,
        NChunkClient::NProto::TChunkSpec* chunkSpec)
    {
        const auto& chunkMeta = chunk->GetChunkMeta();
        const auto& miscExt = GetProtoExtension<TMiscExt>(chunkMeta.extensions());

        ToProto(chunkSpec->mutable_chunk_id(), chunk->GetChunkId());

        // Adjust read ranges.
        if (chunk->IsSorted()) {
            auto sortedStore = chunk->AsSorted();

            if (sortedStore->HasNontrivialReadRange()) {
                // Adjust ranges for chunk views.
                lowerLimit.MergeLowerKey(sortedStore->GetMinKey());
                lowerLimit.MergeUpperKey(sortedStore->GetUpperBoundKey());
            } else {
                // Drop redundant ranges for chunks.
                if (lowerLimit.HasKey() && lowerLimit.GetKey() <= sortedStore->GetMinKey()) {
                    lowerLimit.SetKey({});
                }
                if (upperLimit.HasKey() && upperLimit.GetKey() >= sortedStore->GetUpperBoundKey()) {
                    upperLimit.SetKey({});
                }
            }
        }

        if (!lowerLimit.IsTrivial()) {
            ToProto(chunkSpec->mutable_lower_limit(), lowerLimit);
        }
        if (!upperLimit.IsTrivial()) {
            ToProto(chunkSpec->mutable_upper_limit(), upperLimit);
        }

        auto localNodeId = Bootstrap_->GetMasterConnector()->GetNodeId();
        ToProto(chunkSpec->mutable_replicas(), chunk->GetReplicas(localNodeId));

        chunkSpec->set_erasure_codec(miscExt.erasure_codec());

        chunkSpec->set_row_count_override(miscExt.row_count());
        chunkSpec->set_data_weight_override(miscExt.data_weight());

        *chunkSpec->mutable_chunk_meta() = chunkMeta;
        if (!fetchAllMetaExtensions) {
            FilterProtoExtensions(
                chunkSpec->mutable_chunk_meta()->mutable_extensions(),
                chunkMeta.extensions(),
                extensionTags);
        }

        if (auto overrideTimestamp = chunk->GetOverrideTimestamp()) {
            chunkSpec->set_override_timestamp(overrideTimestamp);
        }
    }

    void BuildDynamicStoreSpec(
        const IDynamicStorePtr& dynamicStore,
        TTabletId tabletId,
        const TReadLimit& lowerLimit,
        const TReadLimit& upperLimit,
        NChunkClient::NProto::TChunkSpec* chunkSpec)
    {
        ToProto(chunkSpec->mutable_chunk_id(), dynamicStore->GetId());
        ToProto(chunkSpec->mutable_tablet_id(), tabletId);

        chunkSpec->set_row_count_override(dynamicStore->GetRowCount());
        // For dynamic stores it is more or less the same.
        chunkSpec->set_data_weight_override(dynamicStore->GetUncompressedDataSize());

        auto localNodeId = Bootstrap_->GetMasterConnector()->GetNodeId();
        TChunkReplica replica(localNodeId, GenericChunkReplicaIndex);
        chunkSpec->add_replicas(ToProto<ui32>(replica));

        if (!lowerLimit.IsTrivial()) {
            ToProto(chunkSpec->mutable_lower_limit(), lowerLimit);
        }
        if (!upperLimit.IsTrivial()) {
            ToProto(chunkSpec->mutable_upper_limit(), upperLimit);
        }
    }

    std::vector<TSharedRef> GatherSamples(
        const NTabletNode::TTabletSnapshotPtr& tabletSnapshot,
        const TLegacyOwningKey& lowerBound,
        const TLegacyOwningKey& upperBound,
        i64 dataSizeBetweenSamples)
    {
        std::vector<TLegacyKey> keys;
        i64 cumulativeSize = 0;
        i64 nextSampleExpectedPosition = dataSizeBetweenSamples;

        auto tryEmitSample = [&] (const TLegacyKey& key, i64 span) {
            if (cumulativeSize >= nextSampleExpectedPosition) {
                keys.push_back(key);
                nextSampleExpectedPosition += dataSizeBetweenSamples;
            } else {
                i64 thisSamplePosition = cumulativeSize;
                i64 nextSamplePosition = cumulativeSize + span;
                if (nextSamplePosition > dataSizeBetweenSamples &&
                    (nextSamplePosition - nextSampleExpectedPosition) >
                        (nextSampleExpectedPosition - thisSamplePosition))
                {
                    keys.push_back(key);
                    nextSampleExpectedPosition += dataSizeBetweenSamples;
                }
            }
            cumulativeSize += span;
        };

        for (const auto& partition : tabletSnapshot->PartitionList) {
            if (partition->PivotKey >= upperBound) {
                break;
            }
            if (partition->NextPivotKey <= lowerBound) {
                continue;
            }

            const auto& samples = partition->SampleKeys->Keys;

            i64 partitionDataSize = 0;
            for (const auto& store : partition->Stores) {
                partitionDataSize += store->GetCompressedDataSize();
            }
            i64 span = partitionDataSize / (samples.size() + 1);

            if (partition->PivotKey >= lowerBound && partition->PivotKey < upperBound) {
                tryEmitSample(partition->PivotKey, span);
            }

            auto firstIt = std::lower_bound(samples.begin(), samples.end(), lowerBound);
            auto lastIt = std::lower_bound(samples.begin(), samples.end(), upperBound);
            while (firstIt < lastIt) {
                tryEmitSample(*firstIt++, span);
            }
        }

        TWireProtocolWriter writer;
        writer.WriteUnversionedRowset(keys);
        return writer.Finish();
    }

    DECLARE_RPC_SERVICE_METHOD(NQueryClient::NProto, FetchTabletStores)
    {
        context->SetRequestInfo("SubrequestCount: %v", request->subrequests_size());

        const auto& slotManager = Bootstrap_->GetTabletSlotManager();

        auto extensionTags = FromProto<THashSet<int>>(request->extension_tags());

        for (const auto& subrequest : request->subrequests()) {
            auto* subresponse = response->add_subresponses();

            auto tabletId = FromProto<TTabletId>(subrequest.tablet_id());
            auto tableIndex = subrequest.table_index();

            try {
                auto tabletSnapshot = subrequest.has_mount_revision()
                    ? slotManager->FindTabletSnapshot(tabletId, subrequest.mount_revision())
                    : slotManager->FindLatestTabletSnapshot(tabletId);
                if (!tabletSnapshot) {
                    subresponse->set_tablet_missing(true);
                    continue;
                }

                if (!tabletSnapshot->PhysicalSchema->IsSorted()) {
                    THROW_ERROR_EXCEPTION("Fetching tablet stores for ordered tablets is not implemented");
                }

                auto validateReadLimit = [] (const TReadLimit& readLimit) {
                    if (readLimit.HasOffset()) {
                        THROW_ERROR_EXCEPTION("Cannot specify offset limit for fetching tablet stores");
                    }
                    if (readLimit.HasRowIndex()) {
                        THROW_ERROR_EXCEPTION("Cannot specify row index limit for fetching tablet stores");
                    }
                    if (readLimit.HasTabletIndex()) {
                        THROW_ERROR_EXCEPTION("Cannot specify tablet index limit for fetching tablet stores");
                    }
                    if (readLimit.HasChunkIndex()) {
                        THROW_ERROR_EXCEPTION("Cannot specify chunk index limit for fetching tablet stores");
                    }
                };

                for (int rangeIndex = 0; rangeIndex < subrequest.ranges_size(); ++rangeIndex) {
                    const auto& protoRange = subrequest.ranges(rangeIndex);
                    auto range = FromProto<TReadRange>(protoRange);
                    validateReadLimit(range.LowerLimit());
                    validateReadLimit(range.UpperLimit());

                    if (subrequest.fetch_samples()) {
                        response->Attachments().emplace_back();
                    }

                    const auto& rangeLowerBound = range.LowerLimit().HasKey()
                        ? range.LowerLimit().GetKey()
                        : MinKey();
                    const auto& rangeUpperBound = range.UpperLimit().HasKey()
                        ? range.UpperLimit().GetKey()
                        : MaxKey();

                    const auto& lowerBound = ChooseMaxKey(rangeLowerBound, tabletSnapshot->PivotKey);
                    const auto& upperBound = ChooseMinKey(rangeUpperBound, tabletSnapshot->NextPivotKey);

                    if (lowerBound >= upperBound) {
                        continue;
                    }

                    TReadLimit inducedLowerBound;
                    TReadLimit inducedUpperBound;
                    if (lowerBound != MinKey()) {
                        inducedLowerBound.SetKey(lowerBound);
                    }
                    if (upperBound != MaxKey()) {
                        inducedUpperBound.SetKey(upperBound);
                    }

                    auto addStore = [&] (const IStorePtr& store) {
                        switch (store->GetType()) {
                            case EStoreType::SortedChunk: {
                                auto sortedStore = store->AsSorted();
                                if (sortedStore->GetMinKey() >= upperBound || sortedStore->GetUpperBoundKey() <= lowerBound) {
                                    return;
                                }

                                BuildChunkSpec(
                                    store->AsChunk(),
                                    inducedLowerBound,
                                    inducedUpperBound,
                                    request->fetch_all_meta_extensions(),
                                    extensionTags,
                                    subresponse->add_stores());

                                break;
                            }

                            case EStoreType::SortedDynamic:
                                if (tabletSnapshot->Config->EnableDynamicStoreRead &&
                                    !request->omit_dynamic_stores())
                                {
                                    BuildDynamicStoreSpec(
                                        store->AsDynamic(),
                                        tabletId,
                                        inducedLowerBound,
                                        inducedUpperBound,
                                        subresponse->add_stores());
                                } else {
                                    return;
                                }

                                break;

                            default:
                                THROW_ERROR_EXCEPTION("Unexpected store type %Qlv",
                                    store->GetType());
                        }

                        auto* spec = subresponse->mutable_stores(subresponse->stores_size() - 1);
                        spec->set_range_index(subrequest.range_indices(rangeIndex));
                        spec->set_table_index(tableIndex);
                    };

                    for (const auto& store : tabletSnapshot->Eden->Stores) {
                        addStore(store);
                    }

                    {
                        const auto& partitions = tabletSnapshot->PartitionList;

                        auto firstIt = std::lower_bound(
                            partitions.begin(),
                            partitions.end(),
                            lowerBound,
                            [&] (const TPartitionSnapshotPtr& lhs, TLegacyKey rhs) {
                                return lhs->NextPivotKey <= rhs;
                            });
                        auto lastIt = std::lower_bound(
                            partitions.begin(),
                            partitions.end(),
                            upperBound,
                            [&] (const TPartitionSnapshotPtr& lhs, TLegacyKey rhs) {
                                return lhs->PivotKey < rhs;
                            });

                        for (auto it = firstIt; it != lastIt; ++it) {
                            for (const auto& store : (*it)->Stores) {
                                addStore(store);
                            }
                        }
                    }

                    if (subrequest.fetch_samples()) {
                        auto samples = GatherSamples(
                            tabletSnapshot,
                            lowerBound,
                            upperBound,
                            subrequest.data_size_between_samples());
                        struct TFetchTabletStoresTag {};
                        auto mergedRef = MergeRefsToRef<TFetchTabletStoresTag>(std::move(samples));
                        response->Attachments().back() = std::move(mergedRef);
                    }
                }
            } catch (const std::exception& ex) {
                THROW_ERROR_EXCEPTION("Error fetching tablet %v stores",
                    tabletId)
                    << TErrorAttribute("tablet_id", tabletId)
                    << ex;
            }
        }

        context->Reply();
    }
};

IServicePtr CreateQueryService(
    TQueryAgentConfigPtr config,
    NClusterNode::TBootstrap* bootstrap)
{
    return New<TQueryService>(config, bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryAgent
