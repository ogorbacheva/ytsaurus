#include "executor.h"
#include "column_evaluator.h"
#include "coordinator.h"
#include "evaluator.h"
#include "helpers.h"
#include "query.h"
#include "query_helpers.h"
#include "query_service_proxy.h"
#include "query_statistics.h"
#include "functions_cache.h"

#include <yt/ytlib/api/config.h>
#include <yt/ytlib/api/native_connection.h>
#include <yt/ytlib/api/tablet_helpers.h>

#include <yt/ytlib/node_tracker_client/channel.h>
#include <yt/ytlib/object_client/helpers.h>

#include <yt/ytlib/tablet_client/table_mount_cache.h>
#include <yt/ytlib/table_client/schemaful_reader.h>

#include <yt/ytlib/tablet_client/wire_protocol.h>

#include <yt/ytlib/hive/cell_directory.h>

#include <yt/core/profiling/scoped_timer.h>

#include <yt/core/compression/codec.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/misc/collection_helpers.h>
#include <yt/core/misc/protobuf_helpers.h>

#include <yt/core/rpc/helpers.h>

namespace NYT {
namespace NQueryClient {

using namespace NConcurrency;
using namespace NTableClient;
using namespace NTabletClient;

using NYT::ToProto;

using NApi::INativeConnectionPtr;
using NApi::ValidateTabletMountedOrFrozen;
using NApi::GetPrimaryTabletPeerDescriptor;

using NChunkClient::NProto::TDataStatistics;

using NNodeTrackerClient::INodeChannelFactoryPtr;

using NObjectClient::TObjectId;
using NObjectClient::FromObjectId;

using NHiveClient::TCellDescriptor;

////////////////////////////////////////////////////////////////////////////////

class TQueryResponseReader
    : public ISchemafulReader
{
public:
    TQueryResponseReader(
        TFuture<TQueryServiceProxy::TRspExecutePtr> asyncResponse,
        const TTableSchema& schema,
        NCompression::ECodec codecId,
        const NLogging::TLogger& logger)
        : Schema_(schema)
        , CodecId_(codecId)
        , Logger(logger)
    {
        // NB: Don't move this assignment to initializer list as
        // OnResponse will access "this", which is not fully constructed yet.
        QueryResult_ = asyncResponse.Apply(BIND(
            &TQueryResponseReader::OnResponse,
            MakeStrong(this)));
    }

    virtual bool Read(std::vector<TUnversionedRow>* rows) override
    {
        auto reader = GetRowsetReader();
        return !reader || reader->Read(rows);
    }

    virtual TFuture<void> GetReadyEvent() override
    {
        auto reader = GetRowsetReader();
        return reader
            ? reader->GetReadyEvent()
            : QueryResult_.As<void>();
    }

    TFuture<TQueryStatistics> GetQueryResult() const
    {
        return QueryResult_;
    }

    virtual TDataStatistics GetDataStatistics() const override
    {
        return TDataStatistics();
    }

private:
    const TTableSchema Schema_;
    const NCompression::ECodec CodecId_;
    const NLogging::TLogger Logger;

    TFuture<TQueryStatistics> QueryResult_;
    ISchemafulReaderPtr RowsetReader_;
    TSpinLock SpinLock_;

    ISchemafulReaderPtr GetRowsetReader()
    {
        TGuard<TSpinLock> guard(SpinLock_);
        return RowsetReader_;
    }

    TQueryStatistics OnResponse(const TQueryServiceProxy::TRspExecutePtr& response)
    {
        TGuard<TSpinLock> guard(SpinLock_);
        YCHECK(!RowsetReader_);
        RowsetReader_ = CreateWireProtocolRowsetReader(
            response->Attachments(),
            CodecId_,
            Schema_,
            false,
            Logger);
        return FromProto(response->query_statistics());
    }
};

DEFINE_REFCOUNTED_TYPE(TQueryResponseReader)

////////////////////////////////////////////////////////////////////////////////

struct TQueryHelperRowBufferTag
{ };

DECLARE_REFCOUNTED_CLASS(TQueryExecutor)

class TQueryExecutor
    : public IExecutor
{
public:
    TQueryExecutor(
        INativeConnectionPtr connection,
        INodeChannelFactoryPtr nodeChannelFactory,
        const TFunctionImplCachePtr& functionImplCache)
        : Connection_(std::move(connection))
        , NodeChannelFactory_(std::move(nodeChannelFactory))
        , FunctionImplCache_(functionImplCache)
    { }

    virtual TFuture<TQueryStatistics> Execute(
        TConstQueryPtr query,
        TConstExternalCGInfoPtr externalCGInfo,
        TDataRanges dataSource,
        ISchemafulWriterPtr writer,
        const TQueryOptions& options) override
    {
        TRACE_CHILD("QueryClient", "Execute") {

            auto execute = query->IsOrdered()
                ? &TQueryExecutor::DoExecuteOrdered
                : &TQueryExecutor::DoExecute;

            return BIND(execute, MakeStrong(this))
                .AsyncVia(Connection_->GetHeavyInvoker())
                .Run(
                    std::move(query),
                    std::move(externalCGInfo),
                    std::move(dataSource),
                    options,
                    std::move(writer));
        }
    }

private:
    const INativeConnectionPtr Connection_;
    const INodeChannelFactoryPtr NodeChannelFactory_;
    const TFunctionImplCachePtr FunctionImplCache_;

    template <class TIterator, class TTraits, class TOnItemsFunctor, class TOnShardsFunctor>
    static void Iterate(
        const TTableMountInfoPtr& tableInfo,
        TIterator itemsBegin,
        TIterator itemsEnd,
        TTraits traits,
        TOnItemsFunctor onItemsFunctor,
        TOnShardsFunctor onShardsFunctor)
    {
        auto nextShardIt = tableInfo->Tablets.begin() + 1;
        for (auto itemsIt = itemsBegin; itemsIt != itemsEnd;) {
            if (traits.Less(tableInfo->UpperCapBound, traits.GetLower(*itemsIt))) {
                ++itemsIt;
                continue;
            }

            if (traits.Less(traits.GetUpper(*itemsIt), tableInfo->LowerCapBound)) {
                ++itemsIt;
                continue;
            }

            YCHECK(!tableInfo->Tablets.empty());

            // Run binary search to find the relevant tablets.
            nextShardIt = std::lower_bound(
                nextShardIt,
                tableInfo->Tablets.end(),
                traits.GetLower(*itemsIt),
                [&] (const TTabletInfoPtr& tabletInfo, TKey key) {
                    return traits.Less(tabletInfo->PivotKey.Get(), key);
                });

            auto startShardIt = nextShardIt - 1;
            auto tabletInfo = *startShardIt;
            auto nextPivotKey = (nextShardIt == tableInfo->Tablets.end())
                ? tableInfo->UpperCapBound
                : (*nextShardIt)->PivotKey;

            if (traits.Less(traits.GetUpper(*itemsIt), nextPivotKey)) {
                auto endItemsIt = std::lower_bound(
                    itemsIt,
                    itemsEnd,
                    nextPivotKey.Get(),
                    [&] (const auto& item, const TRow& pivot) {
                        return traits.Less(traits.GetUpper(item), pivot);
                    });

                onItemsFunctor(itemsIt, endItemsIt, startShardIt);
                itemsIt = endItemsIt;
            } else {
                auto endShardIt = std::upper_bound(
                    nextShardIt,
                    tableInfo->Tablets.end(),
                    traits.GetUpper(*itemsIt),
                    [&] (TKey key, const TTabletInfoPtr& tabletInfo) {
                        return traits.Less(key, tabletInfo->PivotKey.Get());
                    });

                onShardsFunctor(startShardIt, endShardIt, itemsIt);
                ++itemsIt;
            }
        }
    }

    std::vector<std::pair<TDataRanges, Stroka>> InferRanges(
        TConstQueryPtr query,
        const TDataRanges& dataSource,
        const TQueryOptions& options,
        TRowBufferPtr rowBuffer,
        const NLogging::TLogger& Logger)
    {
        const auto& tableId = dataSource.Id;

        auto tableMountCache = Connection_->GetTableMountCache();
        auto tableInfo = WaitFor(tableMountCache->GetTableInfo(FromObjectId(tableId)))
            .ValueOrThrow();

        tableInfo->ValidateDynamic();
        tableInfo->ValidateNotReplicated();

        const auto& cellDirectory = Connection_->GetCellDirectory();
        const auto& networks = Connection_->GetNetworks();

        yhash_map<NTabletClient::TTabletCellId, TCellDescriptor> tabletCellReplicas;

        auto getAddress = [&] (const TTabletInfoPtr& tabletInfo) mutable {
            ValidateTabletMountedOrFrozen(tableInfo, tabletInfo);

            auto insertResult = tabletCellReplicas.insert(std::make_pair(tabletInfo->CellId, TCellDescriptor()));
            auto& descriptor = insertResult.first->second;

            if (insertResult.second) {
                descriptor = cellDirectory->GetDescriptorOrThrow(tabletInfo->CellId);
            }

            // TODO(babenko): pass proper read options
            const auto& peerDescriptor = GetPrimaryTabletPeerDescriptor(descriptor);
            return peerDescriptor.GetAddress(networks);
        };

        const auto& schema = dataSource.Schema;
        std::vector<std::pair<TDataRanges, Stroka>> subsources;

        auto addSubsource = [&] (const TTabletInfoPtr& tabletInfo) -> TDataRanges* {
            TDataRanges dataSource;
            dataSource.Id = tabletInfo->TabletId;
            dataSource.MountRevision = tabletInfo->MountRevision;
            dataSource.Schema = schema;
            dataSource.LookupSupported = tableInfo->IsSorted();

            const auto& address = getAddress(tabletInfo);
            subsources.emplace_back(std::move(dataSource), address);

            return &subsources.back().first;
        };

        if (dataSource.Ranges) {
            auto ranges = dataSource.Ranges;
            YCHECK(!dataSource.Keys);

            if (query->InferRanges) {
                auto prunedRanges = GetPrunedRanges(
                    query,
                    tableId,
                    ranges,
                    rowBuffer,
                    Connection_->GetColumnEvaluatorCache(),
                    BuiltinRangeExtractorMap,
                    options);

                ranges = MakeSharedRange(std::move(prunedRanges), rowBuffer);
                LOG_DEBUG("Splitting %v prunned / %v original ranges (TableId: %v)", prunedRanges.size(), ranges.Size(), tableId);
            } else {
                LOG_DEBUG("Splitting %v ranges (TableId: %v)", ranges.Size(), tableId);
            }

            struct TTraits
            {
                TRow GetLower(const TRowRange& range)
                {
                    return range.first;
                }

                TRow GetUpper(const TRowRange& range)
                {
                    return range.second;
                }

                bool Less(TKey lhs, TRow rhs) const
                {
                    return CompareRows(lhs, rhs) <= 0;
                }
            };

            Iterate(
                tableInfo,
                begin(ranges),
                end(ranges),
                TTraits(),
                [&] (auto rangesIt, auto endRangesIt, auto shardIt) {
                    addSubsource(*shardIt)->Ranges = MakeSharedRange(
                        MakeRange<TRowRange>(rangesIt, endRangesIt),
                        rowBuffer,
                        ranges.GetHolder());
                },
                [&] (auto startShardIt, auto endShardIt, auto rangesIt) {
                    TRow currentBound = rangesIt->first;

                    auto* subsource = addSubsource(*startShardIt++);

                    for (auto it = startShardIt; it != endShardIt; ++it) {
                        const auto& tabletInfo = *it;
                        auto nextBound = rowBuffer->Capture(tabletInfo->PivotKey.Get());
                        subsource->Ranges = MakeSharedRange(
                            SmallVector<TRowRange, 1>{TRowRange{currentBound, nextBound}},
                            rowBuffer,
                            ranges.GetHolder());

                        subsource = addSubsource(tabletInfo);
                        currentBound = nextBound;
                    }

                    subsource->Ranges = MakeSharedRange(
                        SmallVector<TRowRange, 1>{TRowRange{currentBound, rangesIt->second}},
                        rowBuffer,
                        ranges.GetHolder());
                });
        } else {
            YCHECK(!dataSource.Ranges);
            YCHECK(!dataSource.Schema.empty());

            const auto& keys = dataSource.Keys;

            LOG_DEBUG("Splitting %v keys (TableId: %v)", keys.Size(), tableId);

            struct TTraits
            {
                size_t KeySize;

                TRow GetLower(const TRow& row)
                {
                    return row;
                }

                TRow GetUpper(const TRow& row)
                {
                    return row;
                }

                bool Less(TKey lhs, TRow rhs) const
                {
                    return CompareRows(lhs, rhs, KeySize) < 0;
                }
            };

            Iterate(
                tableInfo,
                begin(keys),
                end(keys),
                TTraits{dataSource.Schema.size()},
                [&] (auto keysIt, auto endKeysIt, auto shardIt) {
                    addSubsource(*shardIt)->Keys = MakeSharedRange(
                        MakeRange<TRow>(keysIt, endKeysIt),
                        rowBuffer,
                        keys.GetHolder());
                },
                [&] (auto startShardIt, auto endShardIt, auto keysIt) {
                    TRow currentBound = *keysIt;

                    auto* subsource = addSubsource(*startShardIt++);

                    for (auto it = startShardIt; it != endShardIt; ++it) {
                        const auto& tabletInfo = *it;
                        auto nextBound = rowBuffer->Capture(tabletInfo->PivotKey.Get());
                        subsource->Ranges = MakeSharedRange(
                            SmallVector<TRowRange, 1>{TRowRange{currentBound, nextBound}},
                            rowBuffer,
                            keys.GetHolder());

                        subsource = addSubsource(tabletInfo);
                        currentBound = nextBound;
                    }

                    auto bound = *keysIt;
                    auto upperBound = rowBuffer->AllocateUnversioned(bound.GetCount() + 1);
                    for (int column = 0; column < bound.GetCount(); ++column) {
                        upperBound[column] = bound[column];
                    }
                    upperBound[bound.GetCount()] = MakeUnversionedSentinelValue(EValueType::Max);

                    subsource->Ranges = MakeSharedRange(
                        SmallVector<TRowRange, 1>{TRowRange{currentBound, upperBound}},
                        rowBuffer,
                        keys.GetHolder());
                });
        }

        return subsources;
    }

    TQueryStatistics DoCoordinateAndExecute(
        TConstQueryPtr query,
        const TConstExternalCGInfoPtr& externalCGInfo,
        const TQueryOptions& options,
        ISchemafulWriterPtr writer,
        int subrangesCount,
        std::function<std::pair<std::vector<TDataRanges>, Stroka>(int)> getSubsources)
    {
        auto Logger = MakeQueryLogger(query);

        std::vector<TRefiner> refiners(subrangesCount, [] (
            TConstExpressionPtr expr,
            const TKeyColumns& keyColumns) {
                return expr;
            });

        auto functionGenerators = New<TFunctionProfilerMap>();
        auto aggregateGenerators = New<TAggregateProfilerMap>();
        MergeFrom(functionGenerators.Get(), *BuiltinFunctionCG);
        MergeFrom(aggregateGenerators.Get(), *BuiltinAggregateCG);
        FetchImplementations(
            functionGenerators,
            aggregateGenerators,
            externalCGInfo,
            FunctionImplCache_);

        return CoordinateAndExecute(
            query,
            writer,
            refiners,
            [&] (TConstQueryPtr subquery, int index) {
                std::vector<TDataRanges> dataSources;
                Stroka address;
                std::tie(dataSources, address) = getSubsources(index);

                LOG_DEBUG("Delegating subquery (SubQueryId: %v, Address: %v, MaxSubqueries: %v)",
                    subquery->Id,
                    address,
                    options.MaxSubqueries);

                return Delegate(std::move(subquery), externalCGInfo, options, std::move(dataSources), address);
            },
            [&] (TConstFrontQueryPtr topQuery, ISchemafulReaderPtr reader, ISchemafulWriterPtr writer) {
                LOG_DEBUG("Evaluating top query (TopQueryId: %v)", topQuery->Id);
                auto evaluator = Connection_->GetQueryEvaluator();
                return evaluator->Run(
                    std::move(topQuery),
                    std::move(reader),
                    std::move(writer),
                    functionGenerators,
                    aggregateGenerators,
                    options.EnableCodeCache);
            });
    }

    TQueryStatistics DoExecute(
        TConstQueryPtr query,
        TConstExternalCGInfoPtr externalCGInfo,
        TDataRanges dataSource,
        const TQueryOptions& options,
        ISchemafulWriterPtr writer)
    {
        auto Logger = MakeQueryLogger(query);

        auto rowBuffer = New<TRowBuffer>(TQueryHelperRowBufferTag{});
        auto allSplits = InferRanges(
            query,
            dataSource,
            options,
            rowBuffer,
            Logger);

        LOG_DEBUG("Regrouping %v splits into groups",
            allSplits.size());

        yhash_map<Stroka, std::vector<TDataRanges>> groupsByAddress;
        for (const auto& split : allSplits) {
            const auto& address = split.second;
            groupsByAddress[address].push_back(split.first);
        }

        std::vector<std::pair<std::vector<TDataRanges>, Stroka>> groupedSplits;
        for (const auto& group : groupsByAddress) {
            groupedSplits.emplace_back(group.second, group.first);
        }

        LOG_DEBUG("Regrouped %v splits into %v groups",
            allSplits.size(),
            groupsByAddress.size());

        return DoCoordinateAndExecute(
            query,
            externalCGInfo,
            options,
            writer,
            groupedSplits.size(),
            [&] (int index) {
                return groupedSplits[index];
            });
    }

    TQueryStatistics DoExecuteOrdered(
        TConstQueryPtr query,
        TConstExternalCGInfoPtr externalCGInfo,
        TDataRanges dataSource,
        const TQueryOptions& options,
        ISchemafulWriterPtr writer)
    {
        auto Logger = MakeQueryLogger(query);

        auto rowBuffer = New<TRowBuffer>(TQueryHelperRowBufferTag());
        auto allSplits = InferRanges(
            query,
            dataSource,
            options,
            rowBuffer,
            Logger);

        // Should be already sorted
        LOG_DEBUG("Sorting %v splits", allSplits.size());

        YCHECK(std::is_sorted(
            allSplits.begin(),
            allSplits.end(),
            [] (const std::pair<TDataRanges, Stroka>& lhs, const std::pair<TDataRanges, Stroka>& rhs) {
                const auto& lhsData = lhs.first;
                const auto& rhsData = rhs.first;

                const auto& lhsValue = lhsData.Ranges ? lhsData.Ranges.Begin()->first : *lhsData.Keys.Begin();
                const auto& rhsValue = rhsData.Ranges ? rhsData.Ranges.Begin()->first : *rhsData.Keys.Begin();

                return lhsValue < rhsValue;
            }));

        return DoCoordinateAndExecute(
            query,
            externalCGInfo,
            options,
            writer,
            allSplits.size(),
            [&] (int index) {
                const auto& split = allSplits[index];

                LOG_DEBUG("Delegating to tablet %v at %v",
                    split.first.Id,
                    split.second);

                return std::make_pair(std::vector<TDataRanges>{split.first}, split.second);
            });
    }

    std::pair<ISchemafulReaderPtr, TFuture<TQueryStatistics>> Delegate(
        TConstQueryPtr query,
        const TConstExternalCGInfoPtr& externalCGInfo,
        const TQueryOptions& options,
        std::vector<TDataRanges> dataSources,
        const Stroka& address)
    {
        auto Logger = MakeQueryLogger(query);

        TRACE_CHILD("QueryClient", "Delegate") {
            auto channel = NodeChannelFactory_->CreateChannel(address);
            auto config = Connection_->GetConfig();

            TQueryServiceProxy proxy(channel);
            proxy.SetDefaultTimeout(config->QueryTimeout);

            auto req = proxy.Execute();

            TDuration serializationTime;
            {
                NProfiling::TAggregatingTimingGuard timingGuard(&serializationTime);
                ToProto(req->mutable_query(), query);
                ToProto(req->mutable_external_functions(), externalCGInfo->Functions);
                externalCGInfo->NodeDirectory->DumpTo(req->mutable_node_directory());
                ToProto(req->mutable_options(), options);
                ToProto(req->mutable_data_sources(), dataSources);
                req->set_response_codec(static_cast<int>(config->QueryResponseCodec));
            }

            auto queryFingerprint = InferName(query, true);
            LOG_DEBUG("Sending subquery (Fingerprint: %v, ReadSchema: %v, ResultSchema: %v, SerializationTime: %v, "
                "RequestSize: %v)",
                queryFingerprint,
                query->GetReadSchema(),
                query->GetTableSchema(),
                serializationTime,
                req->ByteSize());

            TRACE_ANNOTATION("serialization_time", serializationTime);
            TRACE_ANNOTATION("request_size", req->ByteSize());

            auto resultReader = New<TQueryResponseReader>(
                req->Invoke(),
                query->GetTableSchema(),
                config->QueryResponseCodec,
                Logger);
            return std::make_pair(resultReader, resultReader->GetQueryResult());
        }
    }
};

DEFINE_REFCOUNTED_TYPE(TQueryExecutor)

IExecutorPtr CreateQueryExecutor(
    INativeConnectionPtr connection,
    INodeChannelFactoryPtr nodeChannelFactory,
    const TFunctionImplCachePtr& functionImplCache)
{
    return New<TQueryExecutor>(connection, nodeChannelFactory, functionImplCache);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
