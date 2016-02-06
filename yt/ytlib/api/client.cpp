#include "client.h"
#include "private.h"
#include "box.h"
#include "config.h"
#include "connection.h"
#include "dispatcher.h"
#include "file_reader.h"
#include "file_writer.h"
#include "journal_reader.h"
#include "journal_writer.h"
#include "rowset.h"
#include "table_reader.h"
#include "transaction.h"

#include <yt/ytlib/chunk_client/chunk_list_ypath_proxy.h>
#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/chunk_replica.h>
#include <yt/ytlib/chunk_client/read_limit.h>
#include <yt/ytlib/chunk_client/chunk_teleporter.h>

#include <yt/ytlib/cypress_client/cypress_ypath_proxy.h>
#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/hive/cell_directory.h>
#include <yt/ytlib/hive/config.h>

#include <yt/ytlib/object_client/helpers.h>
#include <yt/ytlib/object_client/master_ypath_proxy.h>
#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/query_client/column_evaluator.h>
#include <yt/ytlib/query_client/coordinator.h>
#include <yt/ytlib/query_client/evaluator.h>
#include <yt/ytlib/query_client/helpers.h>
#include <yt/ytlib/query_client/plan_fragment.h>
#include <yt/ytlib/query_client/plan_helpers.h>
#include <yt/ytlib/query_client/private.h> // XXX(sandello): refactor BuildLogger
#include <yt/ytlib/query_client/query_preparer.h>
#include <yt/ytlib/query_client/query_service_proxy.h>
#include <yt/ytlib/query_client/query_statistics.h>

#include <yt/ytlib/scheduler/job_prober_service_proxy.h>
#include <yt/ytlib/scheduler/scheduler_service_proxy.h>

#include <yt/ytlib/security_client/group_ypath_proxy.h>
#include <yt/ytlib/security_client/helpers.h>

#include <yt/ytlib/table_client/name_table.h>
#include <yt/ytlib/table_client/schema.h>
#include <yt/ytlib/table_client/schemaful_writer.h>

#include <yt/ytlib/tablet_client/table_mount_cache.h>
#include <yt/ytlib/tablet_client/tablet_service_proxy.h>
#include <yt/ytlib/tablet_client/wire_protocol.h>
#include <yt/ytlib/tablet_client/wire_protocol.pb.h>

#include <yt/ytlib/transaction_client/timestamp_provider.h>
#include <yt/ytlib/transaction_client/transaction_manager.h>

#include <yt/core/compression/helpers.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/profiling/scoped_timer.h>

#include <yt/core/rpc/helpers.h>
#include <yt/core/rpc/scoped_channel.h>

#include <yt/core/ytree/attribute_helpers.h>
#include <yt/core/ytree/fluent.h>
#include <yt/core/ytree/ypath_proxy.h>

// TODO(babenko): refactor this
#include <yt/ytlib/object_client/object_service_proxy.h>
#include <yt/ytlib/table_client/chunk_meta_extensions.h>
#include <yt/ytlib/table_client/schemaful_reader.h>
#include <yt/ytlib/table_client/table_ypath_proxy.h>
#include <yt/ytlib/table_client/row_merger.h>
#include <yt/ytlib/table_client/row_base.h>

namespace NYT {
namespace NApi {

using namespace NConcurrency;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;
using namespace NObjectClient;
using namespace NObjectClient::NProto;
using namespace NCypressClient;
using namespace NTransactionClient;
using namespace NRpc;
using namespace NTableClient;
using namespace NTableClient::NProto;
using namespace NTabletClient;
using namespace NTabletClient::NProto;
using namespace NSecurityClient;
using namespace NQueryClient;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NScheduler;
using namespace NHive;

using NChunkClient::TReadLimit;
using NChunkClient::TReadRange;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TQueryHelper)
DECLARE_REFCOUNTED_CLASS(TClient)
DECLARE_REFCOUNTED_CLASS(TTransaction)

////////////////////////////////////////////////////////////////////////////////

void Serialize(const TUserWorkloadDescriptor& workloadDescriptor, NYson::IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("category").Value(workloadDescriptor.Category)
        .Item("band").Value(workloadDescriptor.Band);
}

void Deserialize(TUserWorkloadDescriptor& workloadDescriptor, INodePtr node)
{
    auto mapNode = node->AsMap();
    auto categoryNode = mapNode->FindChild("category");
    if (categoryNode) {
        workloadDescriptor.Category = ConvertTo<EUserWorkloadCategory>(categoryNode);
    }
    auto bandNode = mapNode->FindChild("band");
    if (bandNode) {
        workloadDescriptor.Band = ConvertTo<int>(bandNode);
    }
}

////////////////////////////////////////////////////////////////////////////////

namespace {

TNameTableToSchemaIdMapping BuildColumnIdMapping(
    const TTableMountInfoPtr& tableInfo,
    const TNameTablePtr& nameTable)
{
    for (const auto& name : tableInfo->KeyColumns) {
        if (!nameTable->FindId(name) && !tableInfo->Schema.GetColumnOrThrow(name).Expression) {
            THROW_ERROR_EXCEPTION("No such key column %Qv",
                name);
        }
    }

    TNameTableToSchemaIdMapping mapping;
    mapping.resize(nameTable->GetSize());
    for (int nameTableId = 0; nameTableId < nameTable->GetSize(); ++nameTableId) {
        const auto& name = nameTable->GetName(nameTableId);
        int schemaId = tableInfo->Schema.GetColumnIndexOrThrow(name);
        mapping[nameTableId] = schemaId;
    }
    return mapping;
}

const TCellPeerDescriptor& GetLeadingTabletPeerDescriptorOrThrow(
    const TCellDescriptor& cellDescriptor)
{
    if (cellDescriptor.Peers.empty()) {
        THROW_ERROR_EXCEPTION("No alive replicas for tablet cell %v",
            cellDescriptor.CellId);
    }

    for (const auto& peerDescriptor : cellDescriptor.Peers) {
        if (peerDescriptor.GetVoting()) {
            return peerDescriptor;
        }
    }

    THROW_ERROR_EXCEPTION("No leading peer is known for tablet cell %v",
        cellDescriptor.CellId);
}

TTabletInfoPtr GetTabletForKey(
    const TTableMountInfoPtr& tableInfo,
    NTableClient::TKey key)
{
    auto tabletInfo = tableInfo->GetTablet(key);
    if (tabletInfo->State != ETabletState::Mounted) {
        THROW_ERROR_EXCEPTION(
            NTabletClient::EErrorCode::TabletNotMounted,
            "Tablet %v of table %v is in %Qlv state",
            tabletInfo->TabletId,
            tableInfo->Path,
            tabletInfo->State)
            << TErrorAttribute("tablet_id", tabletInfo->TabletId);
    }
    return tabletInfo;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

TError TCheckPermissionResult::ToError(const Stroka& user, EPermission permission) const
{
    switch (Action) {
        case NSecurityClient::ESecurityAction::Allow:
            return TError();

        case NSecurityClient::ESecurityAction::Deny: {
            TError error;
            if (ObjectName && SubjectName) {
                error = TError(
                    NSecurityClient::EErrorCode::AuthorizationError,
                    "Access denied: %Qlv permission is denied for %Qv by ACE at %v",
                    permission,
                    *SubjectName,
                    *ObjectName);
            } else {
                error = TError(
                    NSecurityClient::EErrorCode::AuthorizationError,
                    "Access denied: %Qlv permission is not allowed by any matching ACE",
                    permission);
            }
            error.Attributes().Set("user", user);
            error.Attributes().Set("permission", permission);
            if (ObjectId) {
                error.Attributes().Set("denied_by", ObjectId);
            }
            if (SubjectId) {
                error.Attributes().Set("denied_for", SubjectId);
            }
            return error;
        }

        default:
            YUNREACHABLE();
    }
}

////////////////////////////////////////////////////////////////////////////////

class TQueryResponseReader
    : public ISchemafulReader
{
public:
    TQueryResponseReader(
        TFuture<TQueryServiceProxy::TRspExecutePtr> asyncResponse,
        const TTableSchema& schema,
        const NLogging::TLogger& logger)
        : Schema_(schema)
        , Logger(logger)
    {
        QueryResult_ = asyncResponse.Apply(BIND(
            &TQueryResponseReader::OnResponse,
            MakeStrong(this)));
    }

    virtual bool Read(std::vector<TUnversionedRow>* rows) override
    {
        return !RowsetReader_ || RowsetReader_->Read(rows);
    }

    virtual TFuture<void> GetReadyEvent() override
    {
        if (!RowsetReader_) {
            return QueryResult_.As<void>();
        } else {
            return RowsetReader_->GetReadyEvent();
        }
    }

    TFuture<TQueryStatistics> GetQueryResult() const
    {
        return QueryResult_;
    }

private:

    TTableSchema Schema_;
    ISchemafulReaderPtr RowsetReader_;

    TFuture<TQueryStatistics> QueryResult_;

    NLogging::TLogger Logger;

    TQueryStatistics OnResponse(const TQueryServiceProxy::TRspExecutePtr& response)
    {
        TSharedRef data;

        TDuration deserializationTime;
        {
            NProfiling::TAggregatingTimingGuard timingGuard(&deserializationTime);
            data = NCompression::DecompressWithEnvelope(response->Attachments());
        }

        LOG_DEBUG("Received subquery result (DeserializationTime: %v, DataSize: %v)",
            deserializationTime,
            data.Size());

        YCHECK(!RowsetReader_);
        RowsetReader_ = TWireProtocolReader(data).CreateSchemafulRowsetReader(Schema_);

        return FromProto(response->query_statistics());
    }
};

DEFINE_REFCOUNTED_TYPE(TQueryResponseReader)

////////////////////////////////////////////////////////////////////////////////

class TQueryHelper
    : public IExecutor
    , public IPrepareCallbacks
{
public:
    TQueryHelper(
        IConnectionPtr connection,
        IChannelPtr masterChannel,
        IChannelFactoryPtr nodeChannelFactory,
        IFunctionRegistryPtr functionRegistry)
        : Connection_(std::move(connection))
        , MasterChannel_(std::move(masterChannel))
        , NodeChannelFactory_(std::move(nodeChannelFactory))
        , FunctionRegistry_(std::move(functionRegistry))
    { }

    // IPrepareCallbacks implementation.

    virtual TFuture<TDataSplit> GetInitialSplit(
        const TRichYPath& path,
        TTimestamp timestamp) override
    {
        return BIND(&TQueryHelper::DoGetInitialSplit, MakeStrong(this))
            .AsyncVia(Connection_->GetDispatcher()->GetLightInvoker())
            .Run(path, timestamp);
    }

    virtual TFuture<TQueryStatistics> Execute(
        TConstQueryPtr query,
        TDataRanges dataSource,
        ISchemafulWriterPtr writer,
        TQueryOptions options) override
    {
        TRACE_CHILD("QueryClient", "Execute") {

            auto execute = query->IsOrdered()
                ? &TQueryHelper::DoExecuteOrdered
                : &TQueryHelper::DoExecute;

            return BIND(execute, MakeStrong(this))
                .AsyncVia(Connection_->GetDispatcher()->GetHeavyInvoker())
                .Run(std::move(query), std::move(dataSource), std::move(options), std::move(writer));
        }
    }

private:
    const IConnectionPtr Connection_;
    const IChannelPtr MasterChannel_;
    const IChannelFactoryPtr NodeChannelFactory_;
    const IFunctionRegistryPtr FunctionRegistry_;


    TDataSplit DoGetInitialSplit(
        const TRichYPath& path,
        TTimestamp timestamp)
    {
        auto tableMountCache = Connection_->GetTableMountCache();
        auto info = WaitFor(tableMountCache->GetTableInfo(path.GetPath()))
            .ValueOrThrow();

        auto tableSchema = path.GetSchema();

        TDataSplit result;
        SetObjectId(&result, info->TableId);
        SetTableSchema(&result, tableSchema.Get(info->Schema));
        SetKeyColumns(&result, info->KeyColumns);
        SetTimestamp(&result, timestamp);

        return result;
    }

    std::vector<std::pair<TDataRanges, Stroka>> SplitTable(
        TGuid tableId,
        TSharedRange<TRowRange> ranges,
        TRowBufferPtr rowBuffer,
        const NLogging::TLogger& Logger,
        bool verboseLogging)
    {
        YCHECK(TypeFromId(tableId) == EObjectType::Table);

        auto tableMountCache = Connection_->GetTableMountCache();
        auto tableInfo = WaitFor(tableMountCache->GetTableInfo(FromObjectId(tableId)))
            .ValueOrThrow();

        if (!tableInfo->Sorted) {
            THROW_ERROR_EXCEPTION("Expected a sorted table, but got unsorted");
        }

        auto result = tableInfo->Dynamic
            ? SplitDynamicTable(tableId, std::move(ranges), std::move(rowBuffer), std::move(tableInfo))
            : SplitStaticTable(tableId, std::move(ranges), std::move(rowBuffer));

        LOG_DEBUG_IF(verboseLogging, "Got %v sources for input %v",
            result.size(),
            tableId);

        return result;
    }

    std::vector<std::pair<TDataRanges, Stroka>> SplitStaticTable(
        TGuid tableId,
        TSharedRange<TRowRange> ranges,
        TRowBufferPtr rowBuffer)
    {
        std::vector<TReadRange> readRanges;
        for (const auto& range : ranges) {
            readRanges.emplace_back(TReadLimit(TOwningKey(range.first)), TReadLimit(TOwningKey(range.second)));
        }

        // TODO(babenko): refactor and optimize
        TObjectServiceProxy proxy(MasterChannel_);

        // XXX(babenko): multicell
        auto req = TTableYPathProxy::Fetch(FromObjectId(tableId));
        ToProto(req->mutable_ranges(), readRanges);
        req->set_fetch_all_meta_extensions(true);

        auto rsp = WaitFor(proxy.Execute(req))
            .ValueOrThrow();

        auto nodeDirectory = New<NNodeTrackerClient::TNodeDirectory>();
        nodeDirectory->MergeFrom(rsp->node_directory());

        auto chunkSpecs = FromProto<std::vector<NChunkClient::NProto::TChunkSpec>>(rsp->chunks());

        // Remove duplicate chunks.
        std::sort(chunkSpecs.begin(), chunkSpecs.end(), [] (const TDataSplit& lhs, const TDataSplit& rhs) {
            return GetObjectIdFromDataSplit(lhs) < GetObjectIdFromDataSplit(rhs);
        });
        chunkSpecs.erase(
            std::unique(
                chunkSpecs.begin(),
                chunkSpecs.end(),
                [] (const TDataSplit& lhs, const TDataSplit& rhs) {
                    return GetObjectIdFromDataSplit(lhs) == GetObjectIdFromDataSplit(rhs);
                }),
            chunkSpecs.end());

        // Sort chunks by lower bound.
        std::sort(chunkSpecs.begin(), chunkSpecs.end(), [] (const TDataSplit& lhs, const TDataSplit& rhs) {
            return GetLowerBoundFromDataSplit(lhs) < GetLowerBoundFromDataSplit(rhs);
        });

        const auto& networkName = Connection_->GetConfig()->NetworkName;

        for (auto& chunkSpec : chunkSpecs) {
            auto chunkKeyColumns = FindProtoExtension<TKeyColumnsExt>(chunkSpec.chunk_meta().extensions());
            auto chunkSchema = FindProtoExtension<TTableSchemaExt>(chunkSpec.chunk_meta().extensions());

            // TODO(sandello): One day we should validate consistency.
            // Now we just check we do _not_ have any of these.
            YCHECK(!chunkKeyColumns);
            YCHECK(!chunkSchema);

            TOwningKey chunkLowerBound, chunkUpperBound;
            if (TryGetBoundaryKeys(chunkSpec.chunk_meta(), &chunkLowerBound, &chunkUpperBound)) {
                chunkUpperBound = GetKeySuccessor(chunkUpperBound);
                SetLowerBound(&chunkSpec, chunkLowerBound);
                SetUpperBound(&chunkSpec, chunkUpperBound);
            }
        }

        std::vector<std::pair<TDataRanges, Stroka>> subsources;
        for (const auto& range : ranges) {
            auto lowerBound = range.first;
            auto upperBound = range.second;

            // Run binary search to find the relevant chunks.
            auto startIt = std::lower_bound(
                chunkSpecs.begin(),
                chunkSpecs.end(),
                lowerBound,
                [] (const TDataSplit& chunkSpec, TRow key) {
                    return GetUpperBoundFromDataSplit(chunkSpec) <= key;
                });

            for (auto it = startIt; it != chunkSpecs.end(); ++it) {
                const auto& chunkSpec = *it;
                auto keyRange = GetBothBoundsFromDataSplit(chunkSpec);

                if (upperBound <= keyRange.first) {
                    break;
                }

                auto replicas = FromProto<TChunkReplicaList>(chunkSpec.replicas());
                if (replicas.empty()) {
                    auto objectId = GetObjectIdFromDataSplit(chunkSpec);
                    THROW_ERROR_EXCEPTION("No alive replicas for chunk %v",
                        objectId);
                }

                const TChunkReplica& selectedReplica = replicas[RandomNumber(replicas.size())];
                const auto& descriptor = nodeDirectory->GetDescriptor(selectedReplica);
                const auto& address = descriptor.GetAddressOrThrow(networkName);

                TRowRange subrange;
                subrange.first = rowBuffer->Capture(std::max(lowerBound, keyRange.first.Get()));
                subrange.second = rowBuffer->Capture(std::min(upperBound, keyRange.second.Get()));

                TDataRanges dataSource{
                    GetObjectIdFromDataSplit(chunkSpec),
                    MakeSharedRange(SmallVector<TRowRange, 1>{subrange}, rowBuffer, ranges.GetHolder())};

                subsources.emplace_back(dataSource, address);
            }
        }

        return subsources;
    }

    std::vector<std::pair<TDataRanges, Stroka>> SplitDynamicTable(
        TGuid tableId,
        TSharedRange<TRowRange> ranges,
        TRowBufferPtr rowBuffer,
        TTableMountInfoPtr tableInfo)
    {
        if (tableInfo->Tablets.empty()) {
            THROW_ERROR_EXCEPTION("Table %v is neither sorted nor has tablets",
                tableId);
        }

        const auto& cellDirectory = Connection_->GetCellDirectory();
        const auto& networkName = Connection_->GetConfig()->NetworkName;

        yhash_map<NTabletClient::TTabletCellId, TCellDescriptor> tabletCellReplicas;

        auto getAddress = [&] (const TTabletInfoPtr& tabletInfo) mutable {
            auto insertResult = tabletCellReplicas.insert(std::make_pair(tabletInfo->CellId, TCellDescriptor()));
            auto& descriptor = insertResult.first->second;

            if (insertResult.second) {
                descriptor = cellDirectory->GetDescriptorOrThrow(tabletInfo->CellId);
            }

            const auto& peerDescriptor = GetLeadingTabletPeerDescriptorOrThrow(descriptor);
            return peerDescriptor.GetAddress(networkName);
        };

        std::vector<std::pair<TDataRanges, Stroka>> subsources;
        for (auto rangesIt = begin(ranges); rangesIt != end(ranges);) {
            auto lowerBound = rangesIt->first;
            auto upperBound = rangesIt->second;

            // Run binary search to find the relevant tablets.
            auto startIt = std::upper_bound(
                tableInfo->Tablets.begin(),
                tableInfo->Tablets.end(),
                lowerBound,
                [] (TRow key, const TTabletInfoPtr& tabletInfo) {
                    return key < tabletInfo->PivotKey;
                }) - 1;

            auto tabletInfo = *startIt;
            auto nextPivotKey = (startIt + 1 == tableInfo->Tablets.end()) ? MaxKey() : (*(startIt + 1))->PivotKey;

            if (upperBound < nextPivotKey) {
                auto rangesItEnd = std::upper_bound(
                    rangesIt,
                    end(ranges),
                    nextPivotKey.Get(),
                    [] (const TRow& key, const TRowRange& rowRange) {
                        return key < rowRange.second;
                    });

                const auto& address = getAddress(tabletInfo);

                TDataRanges dataSource{
                    tabletInfo->TabletId,
                    MakeSharedRange(
                        MakeRange<TRowRange>(rangesIt, rangesItEnd),
                        rowBuffer,
                        ranges.GetHolder())};

                subsources.emplace_back(dataSource, address);
                rangesIt = rangesItEnd;
            } else {
                for (auto it = startIt; it != tableInfo->Tablets.end(); ++it) {
                    const auto& tabletInfo = *it;
                    YASSERT(upperBound > tabletInfo->PivotKey);

                    const auto& address = getAddress(tabletInfo);

                    auto pivotKey = tabletInfo->PivotKey;
                    auto nextPivotKey = (it + 1 == tableInfo->Tablets.end()) ? MaxKey() : (*(it + 1))->PivotKey;

                    bool isLast = upperBound <= nextPivotKey;

                    TRowRange subrange;
                    subrange.first = it == startIt ? lowerBound : rowBuffer->Capture(pivotKey.Get());
                    subrange.second = isLast ? upperBound : rowBuffer->Capture(nextPivotKey.Get());

                    TDataRanges dataSource{
                        tabletInfo->TabletId,
                        MakeSharedRange(SmallVector<TRowRange, 1>{subrange}, rowBuffer, ranges.GetHolder())};

                    subsources.emplace_back(dataSource, address);

                    if (isLast) {
                        break;
                    }
                }
                ++rangesIt;
            }
        }

        return subsources;
    }

    std::vector<std::pair<TDataRanges, Stroka>> InferRanges(
        TConstQueryPtr query,
        TDataRanges dataSource,
        ui64 rangeExpansionLimit,
        bool verboseLogging,
        TRowBufferPtr rowBuffer,
        const NLogging::TLogger& Logger)
    {
        const auto& tableId = dataSource.Id;
        auto ranges = dataSource.Ranges;

        auto prunedRanges = GetPrunedRanges(
            query,
            tableId,
            ranges,
            rowBuffer,
            Connection_->GetColumnEvaluatorCache(),
            FunctionRegistry_,
            rangeExpansionLimit,
            verboseLogging);

        LOG_DEBUG("Splitting %v pruned splits", prunedRanges.size());

        return SplitTable(
            tableId,
            MakeSharedRange(std::move(prunedRanges), rowBuffer),
            std::move(rowBuffer),
            Logger,
            verboseLogging);
    }

    TQueryStatistics DoCoordinateAndExecute(
        TConstQueryPtr query,
        TQueryOptions options,
        ISchemafulWriterPtr writer,
        int subrangesCount,
        std::function<std::pair<std::vector<TDataRanges>, Stroka>(int)> getSubsources)
    {
        auto Logger = BuildLogger(query);

        std::vector<TRefiner> refiners(subrangesCount, [] (
            TConstExpressionPtr expr,
            const TTableSchema& schema,
            const TKeyColumns& keyColumns) {
                return expr;
            });

        return CoordinateAndExecute(
            query,
            writer,
            refiners,
            [&] (TConstQueryPtr subquery, int index) {
                std::vector<TDataRanges> dataSources;
                Stroka address;
                std::tie(dataSources, address) = getSubsources(index);

                LOG_DEBUG("Delegating subquery (SubqueryId: %v, Address: %v, MaxSubqueries %v)",
                    subquery->Id,
                    address,
                    options.MaxSubqueries);

                return Delegate(std::move(subquery), options, std::move(dataSources), address);
            },
            [&] (TConstQueryPtr topQuery, ISchemafulReaderPtr reader, ISchemafulWriterPtr writer) {
                LOG_DEBUG("Evaluating top query (TopQueryId: %v)", topQuery->Id);
                auto evaluator = Connection_->GetQueryEvaluator();
                return evaluator->Run(
                    std::move(topQuery),
                    std::move(reader),
                    std::move(writer),
                    FunctionRegistry_,
                    options.EnableCodeCache);
            },
            FunctionRegistry_);
    }

    TQueryStatistics DoExecute(
        TConstQueryPtr query,
        TDataRanges dataSource,
        TQueryOptions options,
        ISchemafulWriterPtr writer)
    {
        auto Logger = BuildLogger(query);

        auto rowBuffer = New<TRowBuffer>();
        auto allSplits = InferRanges(
            query,
            dataSource,
            options.RangeExpansionLimit,
            options.VerboseLogging,
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

        return DoCoordinateAndExecute(query, options, writer, groupedSplits.size(), [&] (int index) {
            return groupedSplits[index];
        });
    }

    TQueryStatistics DoExecuteOrdered(
        TConstQueryPtr query,
        TDataRanges dataSource,
        TQueryOptions options,
        ISchemafulWriterPtr writer)
    {
        auto Logger = BuildLogger(query);

        auto rowBuffer = New<TRowBuffer>();
        auto allSplits = InferRanges(
            query,
            dataSource,
            options.RangeExpansionLimit,
            options.VerboseLogging,
            rowBuffer,
            Logger);

        // Should be already sorted
        LOG_DEBUG("Sorting %v splits", allSplits.size());

        std::sort(
            allSplits.begin(),
            allSplits.end(),
            [] (const std::pair<TDataRanges, Stroka>& lhs, const std::pair<TDataRanges, Stroka>& rhs) {
                return lhs.first.Ranges.Begin()->first < rhs.first.Ranges.Begin()->first;
            });

        return DoCoordinateAndExecute(query, options, writer, allSplits.size(), [&] (int index) {
            const auto& split = allSplits[index];

            LOG_DEBUG("Delegating to tablet %v at %v",
                split.first.Id,
                split.second);

            return std::make_pair(std::vector<TDataRanges>(1, split.first), split.second);
        });
    }

   std::pair<ISchemafulReaderPtr, TFuture<TQueryStatistics>> Delegate(
        TConstQueryPtr query,
        TQueryOptions options,
        std::vector<TDataRanges> dataSources,
        const Stroka& address)
    {
        auto Logger = BuildLogger(query);

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
                ToProto(req->mutable_options(), options);
                ToProto(req->mutable_data_sources(), dataSources);

                req->set_response_codec(static_cast<int>(config->QueryResponseCodec));
            }

            LOG_DEBUG("Sending subquery (SerializationTime: %v, RequestSize: %v)",
                serializationTime,
                req->ByteSize());

            TRACE_ANNOTATION("serialization_time", serializationTime);
            TRACE_ANNOTATION("request_size", req->ByteSize());

            auto resultReader = New<TQueryResponseReader>(
                req->Invoke(),
                query->GetTableSchema(),
                Logger);
            return std::make_pair(resultReader, resultReader->GetQueryResult());
        }
    }

};

DEFINE_REFCOUNTED_TYPE(TQueryHelper)

////////////////////////////////////////////////////////////////////////////////

class TClient
    : public IClient
{
public:
    TClient(
        IConnectionPtr connection,
        const TClientOptions& options)
        : Connection_(std::move(connection))
        , Options_(options)
        , Invoker_(Connection_->GetDispatcher()->GetLightInvoker())
        , FunctionRegistry_(Connection_->GetFunctionRegistry())
    {
        auto wrapChannel = [&] (IChannelPtr channel) {
            channel = CreateAuthenticatedChannel(channel, options.User);
            channel = CreateScopedChannel(channel);
            return channel;
        };
        auto wrapChannelFactory = [&] (IChannelFactoryPtr factory) {
            factory = CreateAuthenticatedChannelFactory(factory, options.User);
            return factory;
        };

        auto initMasterChannel = [&] (EMasterChannelKind kind, TCellTag cellTag) {
            // NB: Caching is only possible for the primary master.
            if (kind == EMasterChannelKind::Cache && cellTag != Connection_->GetPrimaryMasterCellTag())
                return;
            MasterChannels_[kind][cellTag] = wrapChannel(Connection_->GetMasterChannelOrThrow(kind, cellTag));
        };
        for (auto kind : TEnumTraits<EMasterChannelKind>::GetDomainValues()) {
            initMasterChannel(kind, Connection_->GetPrimaryMasterCellTag());
            for (auto cellTag : Connection_->GetSecondaryMasterCellTags()) {
                initMasterChannel(kind, cellTag);
            }
        }

        SchedulerChannel_ = wrapChannel(Connection_->GetSchedulerChannel());

        LightChannelFactory_ = wrapChannelFactory(Connection_->GetLightChannelFactory());
        HeavyChannelFactory_ = wrapChannelFactory(Connection_->GetHeavyChannelFactory());

        SchedulerProxy_.reset(new TSchedulerServiceProxy(GetSchedulerChannel()));
        JobProberProxy_.reset(new TJobProberServiceProxy(GetSchedulerChannel()));

        TransactionManager_ = New<TTransactionManager>(
            Connection_->GetConfig()->TransactionManager,
            Connection_->GetConfig()->PrimaryMaster->CellId,
            GetMasterChannelOrThrow(EMasterChannelKind::Leader),
            Connection_->GetTimestampProvider(),
            Connection_->GetCellDirectory());

        QueryHelper_ = New<TQueryHelper>(
            Connection_,
            GetMasterChannelOrThrow(EMasterChannelKind::LeaderOrFollower),
            HeavyChannelFactory_,
            FunctionRegistry_);

        Logger.AddTag("Client: %p", this);
    }


    virtual IConnectionPtr GetConnection() override
    {
        return Connection_;
    }

    virtual IChannelPtr GetMasterChannelOrThrow(
        EMasterChannelKind kind,
        TCellTag cellTag = PrimaryMasterCellTag) override
    {
        const auto& channels = MasterChannels_[kind];
        auto it = channels.find(cellTag == PrimaryMasterCellTag ? Connection_->GetPrimaryMasterCellTag() : cellTag);
        if (it == channels.end()) {
            THROW_ERROR_EXCEPTION("Unknown master cell tag %v",
                cellTag);
        }
        return it->second;
    }

    virtual IChannelPtr GetSchedulerChannel() override
    {
        return SchedulerChannel_;
    }

    virtual IChannelFactoryPtr GetLightNodeChannelFactory() override
    {
        return LightChannelFactory_;
    }

    virtual IChannelFactoryPtr GetHeavyNodeChannelFactory() override
    {
        return HeavyChannelFactory_;
    }

    virtual TTransactionManagerPtr GetTransactionManager() override
    {
        return TransactionManager_;
    }

    virtual NQueryClient::IExecutorPtr GetQueryExecutor() override
    {
        return QueryHelper_;
    }

    virtual TFuture<void> Terminate() override
    {
        TransactionManager_->AbortAll();

        auto error = TError("Client terminated");
        std::vector<TFuture<void>> asyncResults;

        for (auto kind : TEnumTraits<EMasterChannelKind>::GetDomainValues()) {
            for (const auto& pair : MasterChannels_[kind]) {
                auto channel = pair.second;
                asyncResults.push_back(channel->Terminate(error));
            }
        }
        asyncResults.push_back(SchedulerChannel_->Terminate(error));

        return Combine(asyncResults);
    }


    virtual TFuture<ITransactionPtr> StartTransaction(
        ETransactionType type,
        const TTransactionStartOptions& options) override;

    virtual ITransactionPtr AttachTransaction(
        const TTransactionId& transactionId,
        const TTransactionAttachOptions& options) override;

#define DROP_BRACES(...) __VA_ARGS__
#define IMPLEMENT_METHOD(returnType, method, signature, args) \
    virtual TFuture<returnType> method signature override \
    { \
        return Execute( \
            #method, \
            options, \
            BIND( \
                &TClient::Do ## method, \
                MakeStrong(this), \
                DROP_BRACES args)); \
    }

    IMPLEMENT_METHOD(IRowsetPtr, LookupRows, (
        const TYPath& path,
        TNameTablePtr nameTable,
        const std::vector<NTableClient::TKey>& keys,
        const TLookupRowsOptions& options),
        (path, nameTable, keys, options))

    virtual TFuture<IRowsetPtr> LookupRow(
        const TYPath& path,
        TNameTablePtr nameTable,
        NTableClient::TKey key,
        const TLookupRowsOptions& options) override
    {
        return LookupRows(
            path,
            std::move(nameTable),
            std::vector<NTableClient::TKey>(1, key),
            options);
    }

    IMPLEMENT_METHOD(TSelectRowsResult, SelectRows, (
        const Stroka& query,
        const TSelectRowsOptions& options),
        (query, options))
    IMPLEMENT_METHOD(void, MountTable, (
        const TYPath& path,
        const TMountTableOptions& options),
        (path, options))
    IMPLEMENT_METHOD(void, UnmountTable, (
        const TYPath& path,
        const TUnmountTableOptions& options),
        (path, options))
    IMPLEMENT_METHOD(void, RemountTable, (
        const TYPath& path,
        const TRemountTableOptions& options),
        (path, options))
    IMPLEMENT_METHOD(void, ReshardTable, (
        const TYPath& path,
        const std::vector<NTableClient::TKey>& pivotKeys,
        const TReshardTableOptions& options),
        (path, pivotKeys, options))
    IMPLEMENT_METHOD(void, AlterTable, (
        const TYPath& path,
        const TAlterTableOptions& options),
        (path, options))


    IMPLEMENT_METHOD(TYsonString, GetNode, (
        const TYPath& path,
        const TGetNodeOptions& options),
        (path, options))
    IMPLEMENT_METHOD(void, SetNode, (
        const TYPath& path,
        const TYsonString& value,
        const TSetNodeOptions& options),
        (path, value, options))
    IMPLEMENT_METHOD(void, RemoveNode, (
        const TYPath& path,
        const TRemoveNodeOptions& options),
        (path, options))
    IMPLEMENT_METHOD(TYsonString, ListNode, (
        const TYPath& path,
        const TListNodeOptions& options),
        (path, options))
    IMPLEMENT_METHOD(TNodeId, CreateNode, (
        const TYPath& path,
        EObjectType type,
        const TCreateNodeOptions& options),
        (path, type, options))
    IMPLEMENT_METHOD(TLockId, LockNode, (
        const TYPath& path,
        NCypressClient::ELockMode mode,
        const TLockNodeOptions& options),
        (path, mode, options))
    IMPLEMENT_METHOD(TNodeId, CopyNode, (
        const TYPath& srcPath,
        const TYPath& dstPath,
        const TCopyNodeOptions& options),
        (srcPath, dstPath, options))
    IMPLEMENT_METHOD(TNodeId, MoveNode, (
        const TYPath& srcPath,
        const TYPath& dstPath,
        const TMoveNodeOptions& options),
        (srcPath, dstPath, options))
    IMPLEMENT_METHOD(TNodeId, LinkNode, (
        const TYPath& srcPath,
        const TYPath& dstPath,
        const TLinkNodeOptions& options),
        (srcPath, dstPath, options))
    IMPLEMENT_METHOD(void, ConcatenateNodes, (
        const std::vector<TYPath>& srcPaths,
        const TYPath& dstPath,
        TConcatenateNodesOptions options),
        (srcPaths, dstPath, options))
    IMPLEMENT_METHOD(bool, NodeExists, (
        const TYPath& path,
        const TNodeExistsOptions& options),
        (path, options))


    IMPLEMENT_METHOD(TObjectId, CreateObject, (
        EObjectType type,
        const TCreateObjectOptions& options),
        (type, options))


    virtual IFileReaderPtr CreateFileReader(
        const TYPath& path,
        const TFileReaderOptions& options) override
    {
        return NApi::CreateFileReader(this, path, options);
    }

    virtual IFileWriterPtr CreateFileWriter(
        const TYPath& path,
        const TFileWriterOptions& options) override
    {
        return NApi::CreateFileWriter(this, path, options);
    }


    virtual IJournalReaderPtr CreateJournalReader(
        const TYPath& path,
        const TJournalReaderOptions& options) override
    {
        return NApi::CreateJournalReader(this, path, options);
    }

    virtual IJournalWriterPtr CreateJournalWriter(
        const TYPath& path,
        const TJournalWriterOptions& options) override
    {
        return NApi::CreateJournalWriter(this, path, options);
    }

    virtual TFuture<ISchemalessMultiChunkReaderPtr> CreateTableReader(
        const NYPath::TRichYPath& path,
        const TTableReaderOptions& options) override
    {
        return NApi::CreateTableReader(this, path, options);
    }

    IMPLEMENT_METHOD(void, AddMember, (
        const Stroka& group,
        const Stroka& member,
        const TAddMemberOptions& options),
        (group, member, options))
    IMPLEMENT_METHOD(void, RemoveMember, (
        const Stroka& group,
        const Stroka& member,
        const TRemoveMemberOptions& options),
        (group, member, options))
    IMPLEMENT_METHOD(TCheckPermissionResult, CheckPermission, (
        const Stroka& user,
        const TYPath& path,
        EPermission permission,
        const TCheckPermissionOptions& options),
        (user, path, permission, options))

    IMPLEMENT_METHOD(TOperationId, StartOperation, (
        EOperationType type,
        const TYsonString& spec,
        const TStartOperationOptions& options),
        (type, spec, options))
    IMPLEMENT_METHOD(void, AbortOperation, (
        const TOperationId& operationId,
        const TAbortOperationOptions& options),
        (operationId, options))
    IMPLEMENT_METHOD(void, SuspendOperation, (
        const TOperationId& operationId,
        const TSuspendOperationOptions& options),
        (operationId, options))
    IMPLEMENT_METHOD(void, ResumeOperation, (
        const TOperationId& operationId,
        const TResumeOperationOptions& options),
        (operationId, options))

    IMPLEMENT_METHOD(void, DumpJobContext, (
        const TJobId& jobId,
        const TYPath& path,
        const TDumpJobContextOptions& options),
        (jobId, path, options))
    IMPLEMENT_METHOD(TYsonString, StraceJob, (
        const TJobId& jobId,
        const TStraceJobOptions& options),
        (jobId, options))
    IMPLEMENT_METHOD(void, SignalJob, (
        const TJobId& jobId,
        const Stroka& signalName,
        const TSignalJobOptions& options),
        (jobId, signalName, options))
    IMPLEMENT_METHOD(void, AbandonJob, (
        const TJobId& jobId,
        const TAbandonJobOptions& options),
        (jobId, options))

#undef DROP_BRACES
#undef IMPLEMENT_METHOD

private:
    friend class TTransaction;

    const IConnectionPtr Connection_;
    const TClientOptions Options_;

    const IInvokerPtr Invoker_;

    const IFunctionRegistryPtr FunctionRegistry_;

    TEnumIndexedVector<yhash_map<TCellTag, IChannelPtr>, EMasterChannelKind> MasterChannels_;
    IChannelPtr SchedulerChannel_;
    IChannelFactoryPtr LightChannelFactory_;
    IChannelFactoryPtr HeavyChannelFactory_;
    TTransactionManagerPtr TransactionManager_;
    TQueryHelperPtr QueryHelper_;
    std::unique_ptr<TSchedulerServiceProxy> SchedulerProxy_;
    std::unique_ptr<TJobProberServiceProxy> JobProberProxy_;

    NLogging::TLogger Logger = ApiLogger;


    template <class T>
    TFuture<T> Execute(
        const Stroka& commandName,
        const TTimeoutOptions& options,
        TCallback<T()> callback)
    {
        return
            BIND([=, this_ = MakeStrong(this)] () {
                try {
                    LOG_DEBUG("Command started (Command: %v)", commandName);
                    TBox<T> result(callback);
                    LOG_DEBUG("Command completed (Command: %v)", commandName);
                    return result.Unwrap();
                } catch (const std::exception& ex) {
                    LOG_DEBUG(ex, "Command failed (Command: %v)", commandName);
                    throw;
                }
            })
            .AsyncVia(Invoker_)
            .Run()
            .WithTimeout(options.Timeout);
    }

    template <class T>
    auto CallAndRetryIfMetadataCacheIsInconsistent(T callback) -> decltype(callback())
    {
        int retryCount = 0;
        while (true) {
            TError error;

            try {
                return callback();
            } catch (const NYT::TErrorException& ex) {
                error = ex.Error();
            }

            auto config = Connection_->GetConfig();
            if (++retryCount <= config->TableMountInfoUpdateRetryCount) {
                if (error.FindMatching(NTabletClient::EErrorCode::NoSuchTablet) ||
                    error.FindMatching(NTabletClient::EErrorCode::TabletNotMounted))
                {
                    LOG_DEBUG(error, "Got error, will clear table mount cache and retry");
                    auto tabletId = error.Attributes().Get<TTabletId>("tablet_id");
                    auto tableMountCache = Connection_->GetTableMountCache();
                    auto tabletInfo = tableMountCache->FindTablet(tabletId);
                    if (tabletInfo) {
                        tableMountCache->InvalidateTablet(tabletInfo);
                        auto now = Now();
                        auto retryTime = tabletInfo->UpdateTime + config->TableMountInfoUpdateRetryPeriod;
                        if (retryTime > now) {
                            WaitFor(TDelayedExecutor::MakeDelayed(retryTime - now))
                                .ThrowOnError();
                        }
                    }
                    continue;
                }
            }

            THROW_ERROR error;
        }
    }

    TTableMountInfoPtr SyncGetTableInfo(const TYPath& path)
    {
        const auto& tableMountCache = Connection_->GetTableMountCache();
        return WaitFor(tableMountCache->GetTableInfo(path))
            .ValueOrThrow();
    }


    static void GenerateMutationId(IClientRequestPtr request, TMutatingOptions& options)
    {
        if (!options.MutationId) {
            options.MutationId = NRpc::GenerateMutationId();
        }
        SetMutationId(request, options.MutationId, options.Retry);
        ++options.MutationId.Parts32[1];
    }


    TTransactionId GetTransactionId(const TTransactionalOptions& options, bool allowNullTransaction)
    {
        auto transaction = GetTransaction(options, allowNullTransaction, true);
        return transaction ? transaction->GetId() : NullTransactionId;
    }

    NTransactionClient::TTransactionPtr GetTransaction(
        const TTransactionalOptions& options,
        bool allowNullTransaction,
        bool pingTransaction)
    {
        if (!options.TransactionId) {
            if (!allowNullTransaction) {
                THROW_ERROR_EXCEPTION("A valid master transaction is required");
            }
            return nullptr;
        }

        if (TypeFromId(options.TransactionId) != EObjectType::Transaction) {
            THROW_ERROR_EXCEPTION("A valid master transaction is required");
        }

        TTransactionAttachOptions attachOptions;
        attachOptions.Ping = pingTransaction;
        attachOptions.PingAncestors = options.PingAncestors;
        return TransactionManager_->Attach(options.TransactionId, attachOptions);
    }

    void SetTransactionId(
        IClientRequestPtr request,
        const TTransactionalOptions& options,
        bool allowNullTransaction)
    {
        NCypressClient::SetTransactionId(request, GetTransactionId(options, allowNullTransaction));
    }


    void SetPrerequisites(
        IClientRequestPtr request,
        const TPrerequisiteOptions& options)
    {
        if (options.PrerequisiteTransactionIds.empty())
            return;

        auto* prerequisitesExt = request->Header().MutableExtension(TPrerequisitesExt::prerequisites_ext);
        for (const auto& id : options.PrerequisiteTransactionIds) {
            auto* prerequisiteTransaction = prerequisitesExt->add_transactions();
            ToProto(prerequisiteTransaction->mutable_transaction_id(), id);
        }
    }


    static void SetSuppressAccessTracking(
        IClientRequestPtr request,
        const TSuppressableAccessTrackingOptions& commandOptions)
    {
        if (commandOptions.SuppressAccessTracking) {
            NCypressClient::SetSuppressAccessTracking(request, true);
        }
        if (commandOptions.SuppressModificationTracking) {
            NCypressClient::SetSuppressModificationTracking(request, true);
        }
    }


    std::unique_ptr<TObjectServiceProxy> CreateReadProxy(
        const TReadOptions& options,
        TCellTag cellTag = PrimaryMasterCellTag)
    {
        auto channel = GetMasterChannelOrThrow(options.ReadFrom, cellTag);
        return std::make_unique<TObjectServiceProxy>(channel);
    }

    std::unique_ptr<TObjectServiceProxy> CreateWriteProxy(
        TCellTag cellTag = PrimaryMasterCellTag)
    {
        auto channel = GetMasterChannelOrThrow(EMasterChannelKind::Leader, cellTag);
        return std::make_unique<TObjectServiceProxy>(channel);
    }



    class TTabletCellLookupSession
        : public TIntrinsicRefCounted
    {
    public:
        TTabletCellLookupSession(
            TClient* owner,
            const TCellId& cellId,
            const TLookupRowsOptions& options,
            const TNameTableToSchemaIdMapping& idMapping,
            TTableMountInfoPtr tableInfo)
            : CellId_(cellId)
            , Connection_(owner->Connection_)
            , Config_(Connection_->GetConfig())
            , LookupOptions_(options)
            , ClientOptions_(owner->Options_)
            , IdMapping_(idMapping)
            , TableInfo_(std::move(tableInfo))
        { }

        void AddKey(int index, TTabletInfoPtr tabletInfo, NTableClient::TKey key)
        {
            if (Batches_.empty() ||
                Batches_.back()->TabletInfo->TabletId != tabletInfo->TabletId ||
                Batches_.back()->Indexes.size() >= Config_->MaxRowsPerReadRequest)
            {
                Batches_.emplace_back(new TBatch(std::move(tabletInfo)));
            }

            auto& batch = Batches_.back();
            batch->Indexes.push_back(index);
            batch->Keys.push_back(key);
        }

        TFuture<void> Invoke()
        {
            // Do all the heavy lifting here.
            for (auto& batch : Batches_) {
                TReqLookupRows req;
                if (!LookupOptions_.ColumnFilter.All) {
                    ToProto(req.mutable_column_filter()->mutable_indexes(), LookupOptions_.ColumnFilter.Indexes);
                }

                TWireProtocolWriter writer;
                writer.WriteCommand(EWireProtocolCommand::LookupRows);
                writer.WriteMessage(req);
                writer.WriteSchemafulRowset(batch->Keys, IdMapping_.empty() ? nullptr : &IdMapping_);

                batch->RequestData = NCompression::CompressWithEnvelope(
                    writer.Flush(),
                    Config_->LookupRequestCodec);
            }

            const auto& cellDirectory = Connection_->GetCellDirectory();
            const auto& cellDescriptor = cellDirectory->GetDescriptorOrThrow(CellId_);
            const auto& peerDescriptor = GetLeadingTabletPeerDescriptorOrThrow(cellDescriptor);

            const auto& channelFactory = Connection_->GetLightChannelFactory();
            auto channel = channelFactory->CreateChannel(peerDescriptor.GetAddress(Config_->NetworkName));
            channel = CreateAuthenticatedChannel(std::move(channel), ClientOptions_.User);

            InvokeProxy_ = std::make_unique<TQueryServiceProxy>(std::move(channel));
            InvokeProxy_->SetDefaultTimeout(Config_->LookupTimeout);
            InvokeProxy_->SetDefaultRequestAck(false);

            InvokeNextBatch();
            return InvokePromise_;
        }

        void ParseResponse(
            std::vector<TUnversionedRow>* resultRows,
            std::vector<std::unique_ptr<TWireProtocolReader>>* readers)
        {
            auto schemaData = TWireProtocolReader::GetSchemaData(TableInfo_->Schema, LookupOptions_.ColumnFilter);
            for (const auto& batch : Batches_) {
                auto data = NCompression::DecompressWithEnvelope(batch->Response->Attachments());
                auto reader = std::make_unique<TWireProtocolReader>(data);
                for (int index = 0; index < batch->Keys.size(); ++index) {
                    auto row = reader->ReadSchemafulRow(schemaData);
                    (*resultRows)[batch->Indexes[index]] = row;
                }
                readers->push_back(std::move(reader));
            }
        }

    private:
        const TCellId CellId_;
        const IConnectionPtr Connection_;
        const TConnectionConfigPtr Config_;
        const TLookupRowsOptions LookupOptions_;
        const TClientOptions ClientOptions_;
        const TNameTableToSchemaIdMapping IdMapping_;
        const TTableMountInfoPtr TableInfo_;

        struct TBatch
        {
            explicit TBatch(TTabletInfoPtr tabletInfo)
                : TabletInfo(std::move(tabletInfo))
            { }

            TTabletInfoPtr TabletInfo;
            std::vector<int> Indexes;
            std::vector<NTableClient::TKey> Keys;
            std::vector<TSharedRef> RequestData;
            TQueryServiceProxy::TRspReadPtr Response;
        };

        std::vector<std::unique_ptr<TBatch>> Batches_;
        std::unique_ptr<TQueryServiceProxy> InvokeProxy_;
        int InvokeBatchIndex_ = 0;
        TPromise<void> InvokePromise_ = NewPromise<void>();


        void InvokeNextBatch()
        {
            if (InvokeBatchIndex_ >= Batches_.size()) {
                InvokePromise_.Set(TError());
                return;
            }

            const auto& batch = Batches_[InvokeBatchIndex_];

            auto req = InvokeProxy_->Read();
            ToProto(req->mutable_tablet_id(), batch->TabletInfo->TabletId);
            req->set_timestamp(LookupOptions_.Timestamp);
            req->set_response_codec(static_cast<int>(Config_->LookupResponseCodec));
            req->Attachments() = std::move(batch->RequestData);

            req->Invoke().Subscribe(
                BIND(&TTabletCellLookupSession::OnResponse, MakeStrong(this)));
        }

        void OnResponse(const TQueryServiceProxy::TErrorOrRspReadPtr& rspOrError)
        {
            if (rspOrError.IsOK()) {
                Batches_[InvokeBatchIndex_]->Response = rspOrError.Value();
                ++InvokeBatchIndex_;
                InvokeNextBatch();
            } else {
                InvokePromise_.Set(rspOrError);
            }
        }

    };

    typedef TIntrusivePtr<TTabletCellLookupSession> TTabletCellLookupTabletPtr;

    IRowsetPtr DoLookupRows(
        const TYPath& path,
        TNameTablePtr nameTable,
        const std::vector<NTableClient::TKey>& keys,
        const TLookupRowsOptions& options)
    {
        return CallAndRetryIfMetadataCacheIsInconsistent([&] () {
            return DoLookupRowsOnce(path, nameTable, keys, options);
        });
    }

    IRowsetPtr DoLookupRowsOnce(
        const TYPath& path,
        TNameTablePtr nameTable,
        const std::vector<NTableClient::TKey>& keys,
        const TLookupRowsOptions& options)
    {
        auto tableInfo = SyncGetTableInfo(path);

        int schemaColumnCount = static_cast<int>(tableInfo->Schema.Columns().size());
        int keyColumnCount = static_cast<int>(tableInfo->KeyColumns.size());

        ValidateColumnFilter(options.ColumnFilter, schemaColumnCount);

        auto resultSchema = tableInfo->Schema.Filter(options.ColumnFilter);
        auto idMapping = BuildColumnIdMapping(tableInfo, nameTable);

        // NB: The server-side requires the keys to be sorted.
        std::vector<std::pair<NTableClient::TKey, int>> sortedKeys;
        sortedKeys.reserve(keys.size());

        auto rowBuffer = New<TRowBuffer>();

        if (tableInfo->NeedKeyEvaluation) {
            const auto& evaluatorCache = Connection_->GetColumnEvaluatorCache();
            auto evaluator = evaluatorCache->Find(tableInfo->Schema, keyColumnCount);

            for (int index = 0; index < keys.size(); ++index) {
                ValidateClientKey(keys[index], keyColumnCount, tableInfo->Schema, idMapping);
                auto capturedKey = rowBuffer->CaptureAndPermuteRow(keys[index], tableInfo->Schema, idMapping);
                evaluator->EvaluateKeys(capturedKey, rowBuffer);
                sortedKeys.push_back(std::make_pair(capturedKey, index));
            }

            idMapping.clear();
        } else {
            for (int index = 0; index < static_cast<int>(keys.size()); ++index) {
                ValidateClientKey(keys[index], keyColumnCount, tableInfo->Schema, idMapping);
                sortedKeys.push_back(std::make_pair(keys[index], index));
            }
        }
        std::sort(sortedKeys.begin(), sortedKeys.end());

        yhash_map<TCellId, TTabletCellLookupTabletPtr> cellIdToSession;

        for (const auto& pair : sortedKeys) {
            int index = pair.second;
            auto key = pair.first;
            auto tabletInfo = GetTabletForKey(tableInfo, key);
            const auto& cellId = tabletInfo->CellId;
            auto it = cellIdToSession.find(cellId);
            if (it == cellIdToSession.end()) {
                it = cellIdToSession.insert(std::make_pair(
                    cellId,
                    New<TTabletCellLookupSession>(this, cellId, options, idMapping, tableInfo))).first;
            }
            const auto& session = it->second;
            session->AddKey(index, std::move(tabletInfo), key);
        }

        std::vector<TFuture<void>> asyncResults;
        for (const auto& pair : cellIdToSession) {
            const auto& session = pair.second;
            asyncResults.push_back(session->Invoke());
        }

        WaitFor(Combine(asyncResults))
            .ThrowOnError();

        std::vector<TUnversionedRow> resultRows;
        resultRows.resize(keys.size());

        std::vector<std::unique_ptr<TWireProtocolReader>> readers;

        for (const auto& pair : cellIdToSession) {
            const auto& session = pair.second;
            session->ParseResponse(&resultRows, &readers);
        }

        if (!options.KeepMissingRows) {
            resultRows.erase(
                std::remove_if(
                    resultRows.begin(),
                    resultRows.end(),
                    [] (TUnversionedRow row) {
                        return !static_cast<bool>(row);
                    }),
                resultRows.end());
        }

        return CreateRowset(
            std::move(readers),
            resultSchema,
            std::move(resultRows));
    }

    std::pair<IRowsetPtr, TQueryStatistics> DoSelectRows(
        const Stroka& queryString,
        const TSelectRowsOptions& options)
    {
        return CallAndRetryIfMetadataCacheIsInconsistent([&] () {
            return DoSelectRowsOnce(queryString, options);
        });
    }

    std::pair<IRowsetPtr, TQueryStatistics> DoSelectRowsOnce(
        const Stroka& queryString,
        const TSelectRowsOptions& options)
    {
        auto inputRowLimit = options.InputRowLimit.Get(Connection_->GetConfig()->DefaultInputRowLimit);
        auto outputRowLimit = options.OutputRowLimit.Get(Connection_->GetConfig()->DefaultOutputRowLimit);

        TQueryPtr query;
        TDataRanges dataSource;
        std::tie(query, dataSource) = PreparePlanFragment(
            QueryHelper_.Get(),
            queryString,
            FunctionRegistry_,
            inputRowLimit,
            outputRowLimit,
            options.Timestamp);

        TQueryOptions queryOptions;

        queryOptions.Timestamp = options.Timestamp;
        queryOptions.RangeExpansionLimit = options.RangeExpansionLimit;
        queryOptions.VerboseLogging = options.VerboseLogging;
        queryOptions.EnableCodeCache = options.EnableCodeCache;
        queryOptions.MaxSubqueries = options.MaxSubqueries;

        switch (options.WorkloadDescriptor.Category) {
            case EUserWorkloadCategory::Realtime:
                queryOptions.WorkloadDescriptor.Category = EWorkloadCategory::UserRealtime;
                break;
            case EUserWorkloadCategory::Batch:
                queryOptions.WorkloadDescriptor.Category = EWorkloadCategory::UserBatch;
                break;
        }
        queryOptions.WorkloadDescriptor.Band = options.WorkloadDescriptor.Band;

        ISchemafulWriterPtr writer;
        TFuture<IRowsetPtr> asyncRowset;
        std::tie(writer, asyncRowset) = CreateSchemafulRowsetWriter(query->GetTableSchema());

        auto statistics = WaitFor(QueryHelper_->Execute(query, dataSource, writer, queryOptions))
            .ValueOrThrow();

        auto rowset = WaitFor(asyncRowset)
            .ValueOrThrow();

        if (options.FailOnIncompleteResult) {
            if (statistics.IncompleteInput) {
                THROW_ERROR_EXCEPTION("Query terminated prematurely due to excessive input; consider rewriting your query or changing input limit")
                    << TErrorAttribute("input_row_limit", inputRowLimit);
            }
            if (statistics.IncompleteOutput) {
                THROW_ERROR_EXCEPTION("Query terminated prematurely due to excessive output; consider rewriting your query or changing output limit")
                    << TErrorAttribute("output_row_limit", outputRowLimit);
            }
        }

        return std::make_pair(rowset, statistics);
    }

    void DoMountTable(
        const TYPath& path,
        const TMountTableOptions& options)
    {
        auto req = TTableYPathProxy::Mount(path);
        if (options.FirstTabletIndex) {
            req->set_first_tablet_index(*options.FirstTabletIndex);
        }
        if (options.LastTabletIndex) {
            req->set_last_tablet_index(*options.LastTabletIndex);
        }
        if (options.CellId) {
            ToProto(req->mutable_cell_id(), options.CellId);
        }

        auto proxy = CreateWriteProxy();
        WaitFor(proxy->Execute(req))
            .ThrowOnError();
    }

    void DoUnmountTable(
        const TYPath& path,
        const TUnmountTableOptions& options)
    {
        auto req = TTableYPathProxy::Unmount(path);
        if (options.FirstTabletIndex) {
            req->set_first_tablet_index(*options.FirstTabletIndex);
        }
        if (options.LastTabletIndex) {
            req->set_last_tablet_index(*options.LastTabletIndex);
        }
        req->set_force(options.Force);

        auto proxy = CreateWriteProxy();
        WaitFor(proxy->Execute(req))
            .ThrowOnError();
    }

    void DoRemountTable(
        const TYPath& path,
        const TRemountTableOptions& options)
    {
        auto req = TTableYPathProxy::Remount(path);
        if (options.FirstTabletIndex) {
            req->set_first_tablet_index(*options.FirstTabletIndex);
        }
        if (options.LastTabletIndex) {
            req->set_first_tablet_index(*options.LastTabletIndex);
        }

        auto proxy = CreateWriteProxy();
        WaitFor(proxy->Execute(req))
            .ThrowOnError();
    }

    void DoReshardTable(
        const TYPath& path,
        const std::vector<NTableClient::TKey>& pivotKeys,
        const TReshardTableOptions& options)
    {
        auto req = TTableYPathProxy::Reshard(path);
        if (options.FirstTabletIndex) {
            req->set_first_tablet_index(*options.FirstTabletIndex);
        }
        if (options.LastTabletIndex) {
            req->set_last_tablet_index(*options.LastTabletIndex);
        }
        ToProto(req->mutable_pivot_keys(), pivotKeys);

        auto proxy = CreateWriteProxy();
        WaitFor(proxy->Execute(req))
            .ThrowOnError();
    }

    void DoAlterTable(
        const TYPath& path,
        const TAlterTableOptions& options)
    {
        auto req = TTableYPathProxy::Alter(path);
        if (options.Schema) {
            ToProto(req->mutable_schema(), *options.Schema);
        }

        auto proxy = CreateWriteProxy();
        WaitFor(proxy->Execute(req))
            .ThrowOnError();
    }

    TYsonString DoGetNode(
        const TYPath& path,
        const TGetNodeOptions& options)
    {
        auto req = TYPathProxy::Get(path);
        SetTransactionId(req, options, true);
        SetSuppressAccessTracking(req, options);

        if (options.Attributes) {
            ToProto(req->mutable_attributes(), *options.Attributes);
        }
        if (options.MaxSize) {
            req->set_limit(*options.MaxSize);
        }
        req->set_ignore_opaque(options.IgnoreOpaque);
        if (options.Options) {
            ToProto(req->mutable_options(), *options.Options);
        }

        auto proxy = CreateReadProxy(options);
        auto rsp = WaitFor(proxy->Execute(req))
            .ValueOrThrow();

        return TYsonString(rsp->value());
    }

    void DoSetNode(
        const TYPath& path,
        const TYsonString& value,
        TSetNodeOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TYPathProxy::Set(path);
        SetTransactionId(req, options, true);
        GenerateMutationId(req, options);
        req->set_value(value.Data());
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        batchRsp->GetResponse<TYPathProxy::TRspSet>(0)
            .ThrowOnError();
    }

    void DoRemoveNode(
        const TYPath& path,
        TRemoveNodeOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TYPathProxy::Remove(path);
        SetTransactionId(req, options, true);
        GenerateMutationId(req, options);
        req->set_recursive(options.Recursive);
        req->set_force(options.Force);
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        batchRsp->GetResponse<TYPathProxy::TRspRemove>(0)
            .ThrowOnError();
    }

    TYsonString DoListNode(
        const TYPath& path,
        const TListNodeOptions& options)
    {
        auto req = TYPathProxy::List(path);
        SetTransactionId(req, options, true);
        SetSuppressAccessTracking(req, options);

        if (options.Attributes) {
            ToProto(req->mutable_attributes(), *options.Attributes);
        }
        if (options.MaxSize) {
            req->set_limit(*options.MaxSize);
        }

        auto proxy = CreateReadProxy(options);
        auto rsp = WaitFor(proxy->Execute(req))
            .ValueOrThrow();
        return TYsonString(rsp->value());
    }

    TNodeId DoCreateNode(
        const TYPath& path,
        EObjectType type,
        TCreateNodeOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TCypressYPathProxy::Create(path);
        SetTransactionId(req, options, true);
        GenerateMutationId(req, options);
        req->set_type(static_cast<int>(type));
        req->set_recursive(options.Recursive);
        req->set_ignore_existing(options.IgnoreExisting);
        if (options.Attributes) {
            ToProto(req->mutable_node_attributes(), *options.Attributes);
        }
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        auto rsp = batchRsp->GetResponse<TCypressYPathProxy::TRspCreate>(0)
            .ValueOrThrow();
        return FromProto<TNodeId>(rsp->node_id());
    }

    TLockId DoLockNode(
        const TYPath& path,
        NCypressClient::ELockMode mode,
        TLockNodeOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TCypressYPathProxy::Lock(path);
        SetTransactionId(req, options, false);
        GenerateMutationId(req, options);
        req->set_mode(static_cast<int>(mode));
        req->set_waitable(options.Waitable);
        if (options.ChildKey) {
            req->set_child_key(*options.ChildKey);
        }
        if (options.AttributeKey) {
            req->set_attribute_key(*options.AttributeKey);
        }
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        auto rsp = batchRsp->GetResponse<TCypressYPathProxy::TRspLock>(0)
            .ValueOrThrow();
        return FromProto<TLockId>(rsp->lock_id());
    }

    TNodeId DoCopyNode(
        const TYPath& srcPath,
        const TYPath& dstPath,
        TCopyNodeOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TCypressYPathProxy::Copy(dstPath);
        SetTransactionId(req, options, true);
        GenerateMutationId(req, options);
        req->set_source_path(srcPath);
        req->set_preserve_account(options.PreserveAccount);
        req->set_recursive(options.Recursive);
        req->set_force(options.Force);
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        auto rsp = batchRsp->GetResponse<TCypressYPathProxy::TRspCopy>(0)
            .ValueOrThrow();
        return FromProto<TNodeId>(rsp->node_id());
    }

    TNodeId DoMoveNode(
        const TYPath& srcPath,
        const TYPath& dstPath,
        TMoveNodeOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TCypressYPathProxy::Copy(dstPath);
        SetTransactionId(req, options, true);
        GenerateMutationId(req, options);
        req->set_source_path(srcPath);
        req->set_preserve_account(options.PreserveAccount);
        req->set_remove_source(true);
        req->set_recursive(options.Recursive);
        req->set_force(options.Force);
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        auto rsp = batchRsp->GetResponse<TCypressYPathProxy::TRspCopy>(0)
            .ValueOrThrow();
        return FromProto<TNodeId>(rsp->node_id());
    }

    TNodeId DoLinkNode(
        const TYPath& srcPath,
        const TYPath& dstPath,
        TLinkNodeOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TCypressYPathProxy::Create(dstPath);
        req->set_type(static_cast<int>(EObjectType::Link));
        req->set_recursive(options.Recursive);
        req->set_ignore_existing(options.IgnoreExisting);
        SetTransactionId(req, options, true);
        GenerateMutationId(req, options);
        auto attributes = options.Attributes ? ConvertToAttributes(options.Attributes.get()) : CreateEphemeralAttributes();
        attributes->Set("target_path", srcPath);
        ToProto(req->mutable_node_attributes(), *attributes);
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        auto rsp = batchRsp->GetResponse<TCypressYPathProxy::TRspCreate>(0)
            .ValueOrThrow();
        return FromProto<TNodeId>(rsp->node_id());
    }

    void DoConcatenateNodes(
        const std::vector<TYPath>& srcPaths,
        const TYPath& dstPath,
        TConcatenateNodesOptions options)
    {
        try {
            // Get objects ids.
            std::vector<TObjectId> srcIds;
            TCellTagList srcCellTags;
            TObjectId dstId;
            TCellTag dstCellTag;
            {
                auto proxy = CreateReadProxy(options);
                auto batchReq = proxy->ExecuteBatch();

                for (const auto& path : srcPaths) {
                    auto req = TObjectYPathProxy::GetBasicAttributes(path);
                    SetTransactionId(req, options, true);
                    batchReq->AddRequest(req, "get_src_attributes");
                }
                {
                    auto req = TObjectYPathProxy::GetBasicAttributes(dstPath);
                    SetTransactionId(req, options, true);
                    batchReq->AddRequest(req, "get_dst_attributes");
                }

                auto batchRspOrError = WaitFor(batchReq->Invoke());
                THROW_ERROR_EXCEPTION_IF_FAILED(batchRspOrError, "Error getting basic attributes of inputs and outputs");
                const auto& batchRsp = batchRspOrError.Value();

                TNullable<EObjectType> commonType;
                TNullable<Stroka> pathWithCommonType;
                auto checkType = [&] (EObjectType type, const TYPath& path) {
                    if (type != EObjectType::Table && type != EObjectType::File) {
                        THROW_ERROR_EXCEPTION("Type of %v must be either %Qlv or %Qlv",
                            path,
                            EObjectType::Table,
                            EObjectType::File);
                    }
                    if (commonType && *commonType != type) {
                        THROW_ERROR_EXCEPTION("Type of %v (%Qlv) must be the same as type of %v (%Qlv)",
                            path,
                            type,
                            *pathWithCommonType,
                            *commonType);
                    }
                    commonType = type;
                    pathWithCommonType = path;
                };

                {
                    auto rspsOrError = batchRsp->GetResponses<TObjectYPathProxy::TRspGetBasicAttributes>("get_src_attributes");
                    for (int srcIndex = 0; srcIndex < srcPaths.size(); ++srcIndex) {
                        const auto& srcPath = srcPaths[srcIndex];
                        THROW_ERROR_EXCEPTION_IF_FAILED(rspsOrError[srcIndex], "Error getting attributes of %v", srcPath);
                        const auto& rsp = rspsOrError[srcIndex].Value();

                        auto id = FromProto<TObjectId>(rsp->object_id());
                        srcIds.push_back(id);
                        srcCellTags.push_back(rsp->cell_tag());
                        checkType(TypeFromId(id), srcPath);
                    }
                }

                {
                    auto rspsOrError = batchRsp->GetResponses<TObjectYPathProxy::TRspGetBasicAttributes>("get_dst_attributes");
                    THROW_ERROR_EXCEPTION_IF_FAILED(rspsOrError[0], "Error getting attributes of %v", dstPath);
                    const auto& rsp = rspsOrError[0].Value();

                    dstId = FromProto<TObjectId>(rsp->object_id());
                    dstCellTag = rsp->cell_tag();
                    checkType(TypeFromId(dstId), dstPath);
                }
            }

            auto dstIdPath = FromObjectId(dstId);

            // Get source chunk ids.
            // Maps src index -> list of chunk ids for this src.
            std::vector<std::vector<TChunkId>> groupedChunkIds(srcPaths.size());
            {
                yhash_map<TCellTag, std::vector<int>> cellTagToIndexes;
                for (int srcIndex = 0; srcIndex < srcCellTags.size(); ++srcIndex) {
                    cellTagToIndexes[srcCellTags[srcIndex]].push_back(srcIndex);
                }

                for (const auto& pair : cellTagToIndexes) {
                    auto srcCellTag = pair.first;
                    const auto& srcIndexes = pair.second;

                    auto proxy = CreateReadProxy(options, srcCellTag);
                    auto batchReq = proxy->ExecuteBatch();

                    for (int localIndex = 0; localIndex < srcIndexes.size(); ++localIndex) {
                        int srcIndex = srcIndexes[localIndex];
                        auto req = TChunkOwnerYPathProxy::Fetch(FromObjectId(srcIds[srcIndex]));
                        SetTransactionId(req, options, true);
                        ToProto(req->mutable_ranges(), std::vector<TReadRange>{TReadRange()});
                        batchReq->AddRequest(req, "fetch");
                    }

                    auto batchRspOrError = WaitFor(batchReq->Invoke());
                    THROW_ERROR_EXCEPTION_IF_FAILED(batchRspOrError, "Error fetching inputs");

                    const auto& batchRsp = batchRspOrError.Value();
                    auto rspsOrError = batchRsp->GetResponses<TChunkOwnerYPathProxy::TRspFetch>("fetch");
                    for (int localIndex = 0; localIndex < srcIndexes.size(); ++localIndex) {
                        int srcIndex = srcIndexes[localIndex];
                        const auto& rspOrError = rspsOrError[localIndex];
                        const auto& path = srcPaths[srcIndex];
                        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error fetching %v", path);
                        const auto& rsp = rspOrError.Value();

                        for (const auto& chunk : rsp->chunks()) {
                            groupedChunkIds[srcIndex].push_back(FromProto<TChunkId>(chunk.chunk_id()));
                        }
                    }
                }
            }

            // Begin upload.
            TTransactionId uploadTransactionId;
            {
                auto proxy = CreateWriteProxy();

                auto req = TChunkOwnerYPathProxy::BeginUpload(dstIdPath);
                req->set_update_mode(static_cast<int>(options.Append ? EUpdateMode::Append : EUpdateMode::Overwrite));
                req->set_lock_mode(static_cast<int>(options.Append ? ELockMode::Shared : ELockMode::Exclusive));
                req->set_upload_transaction_title(Format("Concatenating %v to %v",
                    srcPaths,
                    dstPath));
                ToProto(req->mutable_upload_transaction_secondary_cell_tags(), srcCellTags);
                req->set_upload_transaction_timeout(ToProto(Connection_->GetConfig()->TransactionManager->DefaultTransactionTimeout));
                GenerateMutationId(req, options);
                SetTransactionId(req, options, true);

                auto rspOrError = WaitFor(proxy->Execute(req));
                THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error starting upload to %v", dstPath);
                const auto& rsp = rspOrError.Value();

                uploadTransactionId = FromProto<TTransactionId>(rsp->upload_transaction_id());
            }

            // Flatten chunk ids.
            std::vector<TChunkId> flatChunkIds;
            for (const auto& ids : groupedChunkIds) {
                flatChunkIds.insert(flatChunkIds.end(), ids.begin(), ids.end());
            }

            // Teleport chunks.
            {
                auto teleporter = New<TChunkTeleporter>(
                    Connection_->GetConfig(),
                    this,
                    Invoker_,
                    uploadTransactionId,
                    Logger);

                for (const auto& chunkId : flatChunkIds) {
                    teleporter->RegisterChunk(chunkId, dstCellTag);
                }

                WaitFor(teleporter->Run())
                    .ThrowOnError();
            }

            // Get upload params.
            TChunkListId chunkListId;
            {
                auto proxy = CreateReadProxy(options, dstCellTag);

                auto req = TChunkOwnerYPathProxy::GetUploadParams(dstIdPath);
                NCypressClient::SetTransactionId(req, uploadTransactionId);

                auto rspOrError = WaitFor(proxy->Execute(req));
                THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error requesting upload parameters for %v", dstPath);
                const auto& rsp = rspOrError.Value();

                chunkListId = FromProto<TChunkListId>(rsp->chunk_list_id());
            }
            
            // Attach chunks to chunk list.
            TDataStatistics dataStatistics;
            {
                auto proxy = CreateWriteProxy(dstCellTag);
                
                auto req = TChunkListYPathProxy::Attach(FromObjectId(chunkListId));
                ToProto(req->mutable_children_ids(), flatChunkIds);
                req->set_request_statistics(true);
                GenerateMutationId(req, options);
            
                auto rspOrError = WaitFor(proxy->Execute(req));
                THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error attaching chunks to %v", dstPath);
                const auto& rsp = rspOrError.Value();

                dataStatistics = rsp->statistics();
            }

            // End upload.
            {
                auto proxy = CreateWriteProxy();

                auto req = TChunkOwnerYPathProxy::EndUpload(dstIdPath);
                *req->mutable_statistics() = dataStatistics;
                NCypressClient::SetTransactionId(req, uploadTransactionId);
                GenerateMutationId(req, options);
                
                auto rspOrError = WaitFor(proxy->Execute(req));
                THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error finishing upload to %v", dstPath);
            }
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error concatenating %v to %v",
                srcPaths,
                dstPath)
                << ex;
        }
    }

    bool DoNodeExists(
        const TYPath& path,
        const TNodeExistsOptions& options)
    {
        auto req = TYPathProxy::Exists(path);
        SetTransactionId(req, options, true);

        auto proxy = CreateReadProxy(options);
        auto rsp = WaitFor(proxy->Execute(req))
            .ValueOrThrow();
        return rsp->value();
    }


    TObjectId DoCreateObject(
        EObjectType type,
        TCreateObjectOptions options)
    {
        auto proxy = CreateWriteProxy();
        auto batchReq = proxy->ExecuteBatch();
        SetPrerequisites(batchReq, options);

        auto req = TMasterYPathProxy::CreateObject();
        GenerateMutationId(req, options);
        if (options.TransactionId) {
            ToProto(req->mutable_transaction_id(), options.TransactionId);
        }
        req->set_type(static_cast<int>(type));
        if (options.Attributes) {
            ToProto(req->mutable_object_attributes(), *options.Attributes);
        }
        batchReq->AddRequest(req);

        auto batchRsp = WaitFor(batchReq->Invoke())
            .ValueOrThrow();
        auto rsp = batchRsp->GetResponse<TMasterYPathProxy::TRspCreateObject>(0)
            .ValueOrThrow();
        return FromProto<TObjectId>(rsp->object_id());
    }


    void DoAddMember(
        const Stroka& group,
        const Stroka& member,
        TAddMemberOptions options)
    {
        auto req = TGroupYPathProxy::AddMember(GetGroupPath(group));
        req->set_name(member);
        GenerateMutationId(req, options);

        auto proxy = CreateWriteProxy();
        WaitFor(proxy->Execute(req))
            .ThrowOnError();
    }

    void DoRemoveMember(
        const Stroka& group,
        const Stroka& member,
        TRemoveMemberOptions options)
    {
        auto req = TGroupYPathProxy::RemoveMember(GetGroupPath(group));
        req->set_name(member);
        GenerateMutationId(req, options);

        auto proxy = CreateWriteProxy();
        WaitFor(proxy->Execute(req))
            .ThrowOnError();
    }

    TCheckPermissionResult DoCheckPermission(
        const Stroka& user,
        const TYPath& path,
        EPermission permission,
        const TCheckPermissionOptions& options)
    {
        auto req = TObjectYPathProxy::CheckPermission(path);
        req->set_user(user);
        req->set_permission(static_cast<int>(permission));
        SetTransactionId(req, options, true);

        auto proxy = CreateReadProxy(options);
        auto rsp = WaitFor(proxy->Execute(req))
            .ValueOrThrow();

        TCheckPermissionResult result;
        result.Action = ESecurityAction(rsp->action());
        result.ObjectId = rsp->has_object_id() ? FromProto<TObjectId>(rsp->object_id()) : NullObjectId;
        result.ObjectName = rsp->has_object_name() ? MakeNullable(rsp->object_name()) : Null;
        result.SubjectId = rsp->has_subject_id() ? FromProto<TSubjectId>(rsp->subject_id()) : NullObjectId;
        result.SubjectName = rsp->has_subject_name() ? MakeNullable(rsp->subject_name()) : Null;
        return result;
    }


    TOperationId DoStartOperation(
        EOperationType type,
        const TYsonString& spec,
        TStartOperationOptions options)
    {
        auto req = SchedulerProxy_->StartOperation();
        SetTransactionId(req, options, true);
        GenerateMutationId(req, options);
        req->set_type(static_cast<int>(type));
        req->set_spec(spec.Data());

        auto rsp = WaitFor(req->Invoke())
            .ValueOrThrow();

        return FromProto<TOperationId>(rsp->operation_id());
    }

    void DoAbortOperation(
        const TOperationId& operationId,
        const TAbortOperationOptions& /*options*/)
    {
        auto req = SchedulerProxy_->AbortOperation();
        ToProto(req->mutable_operation_id(), operationId);

        WaitFor(req->Invoke())
            .ThrowOnError();
    }

    void DoSuspendOperation(
        const TOperationId& operationId,
        const TSuspendOperationOptions& /*options*/)
    {
        auto req = SchedulerProxy_->SuspendOperation();
        ToProto(req->mutable_operation_id(), operationId);

        WaitFor(req->Invoke())
            .ThrowOnError();
    }

    void DoResumeOperation(
        const TOperationId& operationId,
        const TResumeOperationOptions& /*options*/)
    {
        auto req = SchedulerProxy_->ResumeOperation();
        ToProto(req->mutable_operation_id(), operationId);

        WaitFor(req->Invoke())
            .ThrowOnError();
    }


    void DoDumpJobContext(
        const TJobId& jobId,
        const TYPath& path,
        const TDumpJobContextOptions& /*options*/)
    {
        auto req = JobProberProxy_->DumpInputContext();
        ToProto(req->mutable_job_id(), jobId);
        ToProto(req->mutable_path(), path);

        WaitFor(req->Invoke())
            .ThrowOnError();
    }

    TYsonString DoStraceJob(
        const TJobId& jobId,
        const TStraceJobOptions& /*options*/)
    {
        auto req = JobProberProxy_->Strace();
        ToProto(req->mutable_job_id(), jobId);

        auto rsp = WaitFor(req->Invoke())
            .ValueOrThrow();

        return TYsonString(rsp->trace());
    }

    void DoSignalJob(
        const TJobId& jobId,
        const Stroka& signalName,
        const TSignalJobOptions& /*options*/)
    {
        auto req = JobProberProxy_->SignalJob();
        ToProto(req->mutable_job_id(), jobId);
        ToProto(req->mutable_signal_name(), signalName);

        WaitFor(req->Invoke())
            .ThrowOnError();
    }

    void DoAbandonJob(
        const TJobId& jobId,
        const TAbandonJobOptions& /*options*/)
    {
        auto req = JobProberProxy_->AbandonJob();
        ToProto(req->mutable_job_id(), jobId);

        WaitFor(req->Invoke())
            .ThrowOnError();
    }
};

DEFINE_REFCOUNTED_TYPE(TClient)

IClientPtr CreateClient(IConnectionPtr connection, const TClientOptions& options)
{
    YCHECK(connection);

    return New<TClient>(std::move(connection), options);
}

////////////////////////////////////////////////////////////////////////////////

class TTransaction
    : public ITransaction
{
public:
    TTransaction(
        TClientPtr client,
        NTransactionClient::TTransactionPtr transaction)
        : Client_(std::move(client))
        , Transaction_(std::move(transaction))
        , Logger(Client_->Logger)
    {
        Logger.AddTag("TransactionId: %v", GetId());
    }


    virtual IConnectionPtr GetConnection() override
    {
        return Client_->GetConnection();
    }

    virtual IClientPtr GetClient() const override
    {
        return Client_;
    }

    virtual NTransactionClient::ETransactionType GetType() const override
    {
        return Transaction_->GetType();
    }

    virtual const TTransactionId& GetId() const override
    {
        return Transaction_->GetId();
    }

    virtual TTimestamp GetStartTimestamp() const override
    {
        return Transaction_->GetStartTimestamp();
    }

    virtual EAtomicity GetAtomicity() const override
    {
        return Transaction_->GetAtomicity();
    }

    virtual EDurability GetDurability() const override
    {
        return Transaction_->GetDurability();
    }


    virtual TFuture<void> Ping() override
    {
        return Transaction_->Ping();
    }

    virtual TFuture<void> Commit(const TTransactionCommitOptions& options) override
    {
        return BIND(&TTransaction::DoCommit, MakeStrong(this))
            .AsyncVia(Client_->Invoker_)
            .Run(options);
    }

    virtual TFuture<void> Abort(const TTransactionAbortOptions& options) override
    {
        return Transaction_->Abort(options);
    }

    virtual void Detach() override
    {
        Transaction_->Detach();
    }


    virtual void SubscribeAborted(const TClosure& callback) override
    {
        Transaction_->SubscribeAborted(callback);
    }

    virtual void UnsubscribeAborted(const TClosure& callback) override
    {
        Transaction_->UnsubscribeAborted(callback);
    }


    virtual TFuture<ITransactionPtr> StartTransaction(
        ETransactionType type,
        const TTransactionStartOptions& options) override
    {
        auto adjustedOptions = options;
        adjustedOptions.ParentId = GetId();
        return Client_->StartTransaction(
            type,
            adjustedOptions);
    }


    virtual void WriteRow(
        const TYPath& path,
        TNameTablePtr nameTable,
        TUnversionedRow row,
        const TWriteRowsOptions& options) override
    {
        WriteRows(
            path,
            std::move(nameTable),
            std::vector<TUnversionedRow>(1, row),
            options);
    }

    virtual void WriteRows(
        const TYPath& path,
        TNameTablePtr nameTable,
        std::vector<TUnversionedRow> rows,
        const TWriteRowsOptions& options) override
    {
        Requests_.push_back(std::unique_ptr<TRequestBase>(new TWriteRequest(
            this,
            path,
            std::move(nameTable),
            std::move(rows),
            options)));
        LOG_DEBUG("Row writes buffered (RowCount: %v)",
            rows.size());
    }


    virtual void DeleteRow(
        const TYPath& path,
        TNameTablePtr nameTable,
        NTableClient::TKey key,
        const TDeleteRowsOptions& options) override
    {
        DeleteRows(
            path,
            std::move(nameTable),
            std::vector<NTableClient::TKey>(1, key),
            options);
    }

    virtual void DeleteRows(
        const TYPath& path,
        TNameTablePtr nameTable,
        std::vector<NTableClient::TKey> keys,
        const TDeleteRowsOptions& options) override
    {
        Requests_.push_back(std::unique_ptr<TRequestBase>(new TDeleteRequest(
            this,
            path,
            std::move(nameTable),
            std::move(keys),
            options)));
        LOG_DEBUG("Row deletes buffered (RowCount: %v)",
            keys.size());
    }


#define DELEGATE_TRANSACTIONAL_METHOD(returnType, method, signature, args) \
    virtual returnType method signature override \
    { \
        auto& originalOptions = options; \
        { \
            auto options = originalOptions; \
            options.TransactionId = GetId(); \
            return Client_->method args; \
        } \
    }

#define DELEGATE_TIMESTAMPED_METHOD(returnType, method, signature, args) \
    virtual returnType method signature override \
    { \
        auto& originalOptions = options; \
        { \
            auto options = originalOptions; \
            options.Timestamp = GetReadTimestamp(); \
            return Client_->method args; \
        } \
    }

    DELEGATE_TIMESTAMPED_METHOD(TFuture<IRowsetPtr>, LookupRow, (
        const TYPath& path,
        TNameTablePtr nameTable,
        NTableClient::TKey key,
        const TLookupRowsOptions& options),
        (path, nameTable, key, options))
    DELEGATE_TIMESTAMPED_METHOD(TFuture<IRowsetPtr>, LookupRows, (
        const TYPath& path,
        TNameTablePtr nameTable,
        const std::vector<NTableClient::TKey>& keys,
        const TLookupRowsOptions& options),
        (path, nameTable, keys, options))


    DELEGATE_TIMESTAMPED_METHOD(TFuture<TSelectRowsResult>, SelectRows, (
        const Stroka& query,
        const TSelectRowsOptions& options),
        (query, options))


    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TYsonString>, GetNode, (
        const TYPath& path,
        const TGetNodeOptions& options),
        (path, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<void>, SetNode, (
        const TYPath& path,
        const TYsonString& value,
        const TSetNodeOptions& options),
        (path, value, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<void>, RemoveNode, (
        const TYPath& path,
        const TRemoveNodeOptions& options),
        (path, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TYsonString>, ListNode, (
        const TYPath& path,
        const TListNodeOptions& options),
        (path, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TNodeId>, CreateNode, (
        const TYPath& path,
        EObjectType type,
        const TCreateNodeOptions& options),
        (path, type, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TLockId>, LockNode, (
        const TYPath& path,
        NCypressClient::ELockMode mode,
        const TLockNodeOptions& options),
        (path, mode, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TNodeId>, CopyNode, (
        const TYPath& srcPath,
        const TYPath& dstPath,
        const TCopyNodeOptions& options),
        (srcPath, dstPath, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TNodeId>, MoveNode, (
        const TYPath& srcPath,
        const TYPath& dstPath,
        const TMoveNodeOptions& options),
        (srcPath, dstPath, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TNodeId>, LinkNode, (
        const TYPath& srcPath,
        const TYPath& dstPath,
        const TLinkNodeOptions& options),
        (srcPath, dstPath, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<void>, ConcatenateNodes, (
        const std::vector<TYPath>& srcPaths,
        const TYPath& dstPath,
        TConcatenateNodesOptions options),
        (srcPaths, dstPath, options))
    DELEGATE_TRANSACTIONAL_METHOD(TFuture<bool>, NodeExists, (
        const TYPath& path,
        const TNodeExistsOptions& options),
        (path, options))


    DELEGATE_TRANSACTIONAL_METHOD(TFuture<TObjectId>, CreateObject, (
        EObjectType type,
        const TCreateObjectOptions& options),
        (type, options))


    DELEGATE_TRANSACTIONAL_METHOD(IFileReaderPtr, CreateFileReader, (
        const TYPath& path,
        const TFileReaderOptions& options),
        (path, options))
    DELEGATE_TRANSACTIONAL_METHOD(IFileWriterPtr, CreateFileWriter, (
        const TYPath& path,
        const TFileWriterOptions& options),
        (path, options))


    DELEGATE_TRANSACTIONAL_METHOD(IJournalReaderPtr, CreateJournalReader, (
        const TYPath& path,
        const TJournalReaderOptions& options),
        (path, options))
    DELEGATE_TRANSACTIONAL_METHOD(IJournalWriterPtr, CreateJournalWriter, (
        const TYPath& path,
        const TJournalWriterOptions& options),
        (path, options))

    DELEGATE_TRANSACTIONAL_METHOD(TFuture<ISchemalessMultiChunkReaderPtr>, CreateTableReader, (
        const TRichYPath& path,
        const TTableReaderOptions& options),
        (path, options))

#undef DELEGATE_TRANSACTIONAL_METHOD
#undef DELEGATE_TIMESTAMPED_METHOD

    TRowBufferPtr GetRowBuffer() const
    {
        return RowBuffer_;
    }

private:
    const TClientPtr Client_;
    const NTransactionClient::TTransactionPtr Transaction_;

    TRowBufferPtr RowBuffer_ = New<TRowBuffer>();

    NLogging::TLogger Logger;


    class TRequestBase
    {
    public:
        void Run()
        {
            DoPrepare();
            DoRun();
        }

    protected:
        explicit TRequestBase(
            TTransaction* transaction,
            const TYPath& path,
            TNameTablePtr nameTable)
            : Transaction_(transaction)
            , Path_(path)
            , NameTable_(std::move(nameTable))
        { }

        TTransaction* const Transaction_;
        const TYPath Path_;
        const TNameTablePtr NameTable_;

        TTableMountInfoPtr TableInfo_;


        void DoPrepare()
        {
            TableInfo_ = Transaction_->Client_->SyncGetTableInfo(Path_);
        }

        virtual void DoRun() = 0;

    };

    class TModifyRequest
        : public TRequestBase
    {
    protected:
        using TRowValidator = void(TUnversionedRow, int, const TTableSchema&, const TNameTableToSchemaIdMapping&);

        TModifyRequest(
            TTransaction* transaction,
            const TYPath& path,
            TNameTablePtr nameTable)
            : TRequestBase(transaction, path, std::move(nameTable))
        { }

        void WriteRequests(
            const std::vector<TUnversionedRow>& rows,
            EWireProtocolCommand command,
            int columnCount,
            TRowValidator validateRow,
            const TWriteRowsOptions& writeOptions = TWriteRowsOptions())
        {
            const auto& idMapping = Transaction_->GetColumnIdMapping(TableInfo_, NameTable_);
            int keyColumnCount = TableInfo_->KeyColumns.size();
            const auto& schema = TableInfo_->Schema;
            const auto& rowBuffer = Transaction_->GetRowBuffer();
            auto evaluatorCache = Transaction_->GetConnection()->GetColumnEvaluatorCache();
            auto evaluator = TableInfo_->NeedKeyEvaluation
                ? evaluatorCache->Find(TableInfo_->Schema, keyColumnCount)
                : nullptr;

            for (auto row : rows) {
                validateRow(row, keyColumnCount, TableInfo_->Schema, idMapping);

                auto capturedRow = rowBuffer->CaptureAndPermuteRow(row, TableInfo_->Schema, idMapping);

                for (int index = keyColumnCount; index < capturedRow.GetCount(); ++index) {
                    auto& value = capturedRow[index];
                    const auto& columnSchema = schema.Columns()[value.Id];
                    value.Aggregate = columnSchema.Aggregate ? writeOptions.Aggregate : false;
                }

                if (evaluator) {
                    evaluator->EvaluateKeys(capturedRow, rowBuffer);
                }

                auto tabletInfo = GetTabletForKey(TableInfo_, capturedRow);
                auto* session = Transaction_->GetTabletSession(tabletInfo, TableInfo_);
                session->SubmitRow(command, capturedRow);
            }
        }
    };

    class TWriteRequest
        : public TModifyRequest
    {
    public:
        TWriteRequest(
            TTransaction* transaction,
            const TYPath& path,
            TNameTablePtr nameTable,
            std::vector<TUnversionedRow> rows,
            const TWriteRowsOptions& options)
            : TModifyRequest(transaction, path, std::move(nameTable))
            , Rows_(std::move(rows))
            , Options_(options)
        { }

    private:
        const std::vector<TUnversionedRow> Rows_;

        virtual void DoRun() override
        {
            WriteRequests(
                Rows_,
                EWireProtocolCommand::WriteRow,
                TableInfo_->Schema.Columns().size(),
                ValidateClientDataRow,
                Options_);
        }

        TWriteRowsOptions Options_;
    };

    class TDeleteRequest
        : public TModifyRequest
    {
    public:
        TDeleteRequest(
            TTransaction* transaction,
            const TYPath& path,
            TNameTablePtr nameTable,
            std::vector<NTableClient::TKey> keys,
            const TDeleteRowsOptions& /*options*/)
            : TModifyRequest(transaction, path, std::move(nameTable))
            , Keys_(std::move(keys))
        { }

    private:
        const std::vector<TUnversionedRow> Keys_;

        virtual void DoRun() override
        {
            WriteRequests(
                Keys_,
                EWireProtocolCommand::DeleteRow,
                TableInfo_->KeyColumns.size(),
                ValidateClientKey);
        }
    };

    std::vector<std::unique_ptr<TRequestBase>> Requests_;

    class TTabletCommitSession
        : public TIntrinsicRefCounted
    {
    public:
        TTabletCommitSession(
            TTransactionPtr owner,
            TTabletInfoPtr tabletInfo,
            TTableMountInfoPtr tableInfo,
            TColumnEvaluatorPtr columnEvauator)
            : TransactionId_(owner->Transaction_->GetId())
            , TableInfo_(std::move(tableInfo))
            , TabletInfo_(std::move(tabletInfo))
            , TabletId_(TabletInfo_->TabletId)
            , Config_(owner->Client_->Connection_->GetConfig())
            , Durability_(owner->Transaction_->GetDurability())
            , KeyColumnCount_(TableInfo_->KeyColumns.size())
            , ColumnEvaluator_(std::move(columnEvauator))
            , RowBuffer_(New<TRowBuffer>())
            , Logger(owner->Logger)
        {
            Logger.AddTag("TabletId: %v", TabletInfo_->TabletId);
        }

        TWireProtocolWriter* GetWriter()
        {
            if (Batches_.empty() || Batches_.back()->RowCount >= Config_->MaxRowsPerWriteRequest) {
                Batches_.emplace_back(new TBatch());
            }
            auto& batch = Batches_.back();
            ++batch->RowCount;
            return &batch->Writer;
        }

        void SubmitRow(
            EWireProtocolCommand command,
            TUnversionedRow row)
        {
            SubmittedRows_.push_back(TSubmittedRow{
                command,
                row,
                static_cast<int>(SubmittedRows_.size())});
        }

        TFuture<void> Invoke(IChannelPtr channel)
        {
            try {
                std::sort(
                    SubmittedRows_.begin(),
                    SubmittedRows_.end(),
                    [=] (const TSubmittedRow& lhs, const TSubmittedRow& rhs) {
                        int res = CompareRows(lhs.Row, rhs.Row, KeyColumnCount_);
                        return res != 0 ? res < 0 : lhs.SequentialId < rhs.SequentialId;
                    });
            } catch (const std::exception& ex) {
                // NB: CompareRows may throw on composite values.
                return MakeFuture(TError(ex));
            }

            std::vector<TSubmittedRow> mergedRows;
            mergedRows.reserve(SubmittedRows_.size());
            auto merger = New<TUnversionedRowMerger>(
                RowBuffer_,
                KeyColumnCount_,
                ColumnEvaluator_);

            auto addPartialRow = [&] (const TSubmittedRow& submittedRow) {
                switch (submittedRow.Command) {
                    case EWireProtocolCommand::DeleteRow:
                        merger->DeletePartialRow(submittedRow.Row);
                        break;

                    case EWireProtocolCommand::WriteRow:
                        merger->AddPartialRow(submittedRow.Row);
                        break;

                    default:
                        YUNREACHABLE();
                }
            };

            int index = 0;
            while (index < SubmittedRows_.size()) {
                if (index < SubmittedRows_.size() - 1 &&
                    CompareRows(SubmittedRows_[index].Row, SubmittedRows_[index + 1].Row, KeyColumnCount_) == 0)
                {
                    addPartialRow(SubmittedRows_[index]);
                    while (index < SubmittedRows_.size() - 1 &&
                        CompareRows(SubmittedRows_[index].Row, SubmittedRows_[index + 1].Row, KeyColumnCount_) == 0)
                    {
                        ++index;
                        addPartialRow(SubmittedRows_[index]);
                    }
                    SubmittedRows_[index].Row = merger->BuildMergedRow();
                }
                mergedRows.push_back(SubmittedRows_[index]);
                ++index;
            }

            SubmittedRows_ = std::move(mergedRows);

            for (const auto& submittedRow : SubmittedRows_) {
                WriteRow(submittedRow);
            }

            // Do all the heavy lifting here.
            YCHECK(!Batches_.empty());
            for (auto& batch : Batches_) {
                batch->RequestData = NCompression::CompressWithEnvelope(
                    batch->Writer.Flush(),
                    Config_->WriteRequestCodec);;
            }

            merger->Reset();

            InvokeChannel_ = channel;
            InvokeNextBatch();
            return InvokePromise_;
        }

    private:
        const TTransactionId TransactionId_;
        const TTableMountInfoPtr TableInfo_;
        const TTabletInfoPtr TabletInfo_;
        const TTabletId TabletId_;
        const TConnectionConfigPtr Config_;
        const EDurability Durability_;
        const int KeyColumnCount_;

        TColumnEvaluatorPtr ColumnEvaluator_;
        TRowBufferPtr RowBuffer_;

        NLogging::TLogger Logger;

        struct TBatch
        {
            TWireProtocolWriter Writer;
            std::vector<TSharedRef> RequestData;
            int RowCount = 0;
        };

        std::vector<std::unique_ptr<TBatch>> Batches_;

        struct TSubmittedRow
        {
            EWireProtocolCommand Command;
            TUnversionedRow Row;
            int SequentialId;
        };

        std::vector<TSubmittedRow> SubmittedRows_;

        IChannelPtr InvokeChannel_;
        int InvokeBatchIndex_ = 0;
        TPromise<void> InvokePromise_ = NewPromise<void>();

        void WriteRow(const TSubmittedRow& submittedRow)
        {
            if (Batches_.empty() || Batches_.back()->RowCount >= Config_->MaxRowsPerWriteRequest) {
                Batches_.emplace_back(new TBatch());
            }
            auto& batch = Batches_.back();
            ++batch->RowCount;
            auto& writer = batch->Writer;
            writer.WriteCommand(submittedRow.Command);

            switch (submittedRow.Command) {
                case EWireProtocolCommand::DeleteRow: {
                    auto req = TReqDeleteRow();
                    writer.WriteMessage(req);
                    break;
                }

                case EWireProtocolCommand::WriteRow: {
                    auto req = TReqWriteRow();
                    writer.WriteMessage(req);
                    break;
                }

                default:
                    YUNREACHABLE();
            }

            writer.WriteUnversionedRow(submittedRow.Row);
        }

        void InvokeNextBatch()
        {
            if (InvokeBatchIndex_ >= Batches_.size()) {
                InvokePromise_.Set(TError());
                return;
            }

            const auto& batch = Batches_[InvokeBatchIndex_];

            LOG_DEBUG("Sending batch (BatchIndex: %v/%v, RowCount: %v)",
                InvokeBatchIndex_,
                Batches_.size(),
                batch->RowCount);

            TTabletServiceProxy proxy(InvokeChannel_);
            proxy.SetDefaultTimeout(Config_->WriteTimeout);
            proxy.SetDefaultRequestAck(false);

            auto req = proxy.Write();
            ToProto(req->mutable_transaction_id(), TransactionId_);
            ToProto(req->mutable_tablet_id(), TabletInfo_->TabletId);
            req->set_mount_revision(TabletInfo_->MountRevision);
            req->set_durability(static_cast<int>(Durability_));
            req->Attachments() = std::move(batch->RequestData);

            req->Invoke().Subscribe(
                BIND(&TTabletCommitSession::OnResponse, MakeStrong(this)));
        }

        void OnResponse(const TTabletServiceProxy::TErrorOrRspWritePtr& rspOrError)
        {
            if (rspOrError.IsOK()) {
                LOG_DEBUG("Batch sent successfully");
                ++InvokeBatchIndex_;
                InvokeNextBatch();
            } else {
                LOG_DEBUG(rspOrError, "Error sending batch");
                InvokePromise_.Set(rspOrError);
            }
        }

    };

    typedef TIntrusivePtr<TTabletCommitSession> TTabletSessionPtr;

    yhash_map<TTabletInfoPtr, TTabletSessionPtr> TabletToSession_;

    std::vector<TFuture<void>> AsyncTransactionStartResults_;

    // Maps ids from name table to schema, for each involved name table.
    yhash_map<TNameTablePtr, TNameTableToSchemaIdMapping> NameTableToIdMapping_;


    const TNameTableToSchemaIdMapping& GetColumnIdMapping(const TTableMountInfoPtr& tableInfo, const TNameTablePtr& nameTable)
    {
        auto it = NameTableToIdMapping_.find(nameTable);
        if (it == NameTableToIdMapping_.end()) {
            auto mapping = BuildColumnIdMapping(tableInfo, nameTable);
            it = NameTableToIdMapping_.insert(std::make_pair(nameTable, std::move(mapping))).first;
        }
        return it->second;
    }

    TTabletCommitSession* GetTabletSession(const TTabletInfoPtr& tabletInfo, const TTableMountInfoPtr& tableInfo)
    {
        auto it = TabletToSession_.find(tabletInfo);
        if (it == TabletToSession_.end()) {
            AsyncTransactionStartResults_.push_back(Transaction_->AddTabletParticipant(tabletInfo->CellId));
            auto evaluatorCache = GetConnection()->GetColumnEvaluatorCache();
            auto evaluator = evaluatorCache->Find(tableInfo->Schema, tableInfo->KeyColumns.size());
            it = TabletToSession_.insert(std::make_pair(
                tabletInfo,
                New<TTabletCommitSession>(
                    this,
                    tabletInfo,
                    tableInfo,
                    evaluator)
                )).first;
        }
        return it->second.Get();
    }

    void DoCommit(const TTransactionCommitOptions& options)
    {
        try {
            for (const auto& request : Requests_) {
                request->Run();
            }

            WaitFor(Combine(AsyncTransactionStartResults_))
                .ThrowOnError();

            std::vector<TFuture<void>> asyncResults;
            for (const auto& pair : TabletToSession_) {
                const auto& tabletInfo = pair.first;
                const auto& session = pair.second;
                auto channel = GetTabletChannelOrThrow(tabletInfo->CellId);
                asyncResults.push_back(session->Invoke(std::move(channel)));
            }

            WaitFor(Combine(asyncResults))
                .ThrowOnError();
        } catch (const std::exception& ex) {
            // Fire and forget.
            Transaction_->Abort();
            throw;
        }

        WaitFor(Transaction_->Commit(options))
            .ThrowOnError();
    }

    IChannelPtr GetTabletChannelOrThrow(const TTabletCellId& cellId) const
    {
        const auto& cellDirectory = Client_->Connection_->GetCellDirectory();
        auto channel = cellDirectory->GetChannelOrThrow(cellId);
        return CreateAuthenticatedChannel(std::move(channel), Client_->Options_.User);
    }

    TTimestamp GetReadTimestamp() const
    {
        switch (Transaction_->GetAtomicity()) {
            case EAtomicity::Full:
                return GetStartTimestamp();
            case EAtomicity::None:
                // NB: Start timestamp is approximate.
                return SyncLastCommittedTimestamp;
            default:
                YUNREACHABLE();
        }
    }

};

DEFINE_REFCOUNTED_TYPE(TTransaction)

TFuture<ITransactionPtr> TClient::StartTransaction(
    ETransactionType type,
    const TTransactionStartOptions& options)
{
    return TransactionManager_->Start(type, options).Apply(
        BIND([=, this_ = MakeStrong(this)] (NTransactionClient::TTransactionPtr transaction) -> ITransactionPtr {
            return New<TTransaction>(this_, transaction);
        }));
}

ITransactionPtr TClient::AttachTransaction(
    const TTransactionId& transactionId,
    const TTransactionAttachOptions& options)
{
    auto transaction = TransactionManager_->Attach(transactionId, options);
    return New<TTransaction>(this, transaction);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NApi
} // namespace NYT

