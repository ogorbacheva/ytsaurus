#include "helpers.h"
#include "aggregated_job_statistics.h"
#include "table.h"
#include "job_info.h"

#include <yt/yt/server/controller_agent/controllers/task_host.h>

#include <yt/yt/server/controller_agent/config.h>

#include <yt/yt/ytlib/api/native/client.h>

#include <yt/yt/ytlib/chunk_client/data_source.h>
#include <yt/yt/ytlib/chunk_client/data_sink.h>

#include <yt/yt/ytlib/object_client/object_service_proxy.h>

#include <yt/yt/ytlib/controller_agent/proto/output_result.pb.h>

#include <yt/yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/yt/ytlib/table_client/table_ypath_proxy.h>

#include <yt/yt/client/formats/config.h>

#include <yt/yt/client/table_client/row_buffer.h>

#include <util/string/split.h>

namespace NYT::NControllerAgent::NControllers {

using namespace NApi;
using namespace NChunkClient;
using namespace NChunkPools;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NObjectClient;
using namespace NTableClient;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;

using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

static constexpr auto& Logger = ControllerLogger;

////////////////////////////////////////////////////////////////////////////////

TBoundaryKeys BuildBoundaryKeysFromOutputResult(
    const NProto::TOutputResult& boundaryKeys,
    const TOutputStreamDescriptorPtr& streamDescriptor,
    const TRowBufferPtr& rowBuffer)
{
    YT_VERIFY(!boundaryKeys.empty());
    YT_VERIFY(boundaryKeys.sorted());
    YT_VERIFY(!streamDescriptor->TableWriterOptions->ValidateUniqueKeys || boundaryKeys.unique_keys());

    auto trimAndCaptureKey = [&] (const TLegacyOwningKey& key) {
        int limit = streamDescriptor->TableUploadOptions.TableSchema->GetKeyColumnCount();
        if (key.GetCount() > limit) {
            // NB: This can happen for a teleported chunk from a table with a wider key in sorted (but not unique_keys) mode.
            YT_VERIFY(!streamDescriptor->TableWriterOptions->ValidateUniqueKeys);
            return TKey::FromRowUnchecked(rowBuffer->CaptureRow(MakeRange(key.Begin(), limit)), limit);
        } else {
            return TKey::FromRowUnchecked(rowBuffer->CaptureRow(MakeRange(key.Begin(), key.GetCount())), key.GetCount());
        }
    };

    return TBoundaryKeys {
        trimAndCaptureKey(FromProto<TLegacyOwningKey>(boundaryKeys.min())),
        trimAndCaptureKey(FromProto<TLegacyOwningKey>(boundaryKeys.max())),
    };
}

////////////////////////////////////////////////////////////////////////////////

TDataSourceDirectoryPtr BuildDataSourceDirectoryFromInputTables(const std::vector<TInputTablePtr>& inputTables)
{
    auto dataSourceDirectory = New<TDataSourceDirectory>();
    for (const auto& inputTable : inputTables) {
        auto dataSource = (inputTable->Dynamic && inputTable->Schema->IsSorted())
            ? MakeVersionedDataSource(
                inputTable->GetPath(),
                inputTable->Schema,
                inputTable->Path.GetColumns(),
                inputTable->OmittedInaccessibleColumns,
                inputTable->Path.GetTimestamp().value_or(AsyncLastCommittedTimestamp),
                inputTable->Path.GetRetentionTimestamp().value_or(NullTimestamp),
                inputTable->ColumnRenameDescriptors)
            : MakeUnversionedDataSource(
                inputTable->GetPath(),
                inputTable->Schema,
                inputTable->Path.GetColumns(),
                inputTable->OmittedInaccessibleColumns,
                inputTable->ColumnRenameDescriptors);

        dataSource.SetObjectId(inputTable->ObjectId);
        dataSource.SetAccount(inputTable->Account);
        dataSource.SetForeign(inputTable->IsForeign());
        dataSource.SetClusterName(inputTable->ClusterName);
        dataSourceDirectory->DataSources().push_back(dataSource);
    }

    return dataSourceDirectory;
}

NChunkClient::TDataSink BuildDataSinkFromOutputTable(const TOutputTablePtr& outputTable)
{
    TDataSink dataSink;
    dataSink.SetPath(outputTable->GetPath());
    dataSink.SetObjectId(outputTable->ObjectId);
    dataSink.SetAccount(outputTable->Account);
    return dataSink;
}

TDataSinkDirectoryPtr BuildDataSinkDirectoryFromOutputTables(const std::vector<TOutputTablePtr>& outputTables)
{
    auto dataSinkDirectory = New<TDataSinkDirectory>();
    dataSinkDirectory->DataSinks().reserve(outputTables.size());
    for (const auto& outputTable : outputTables) {
        dataSinkDirectory->DataSinks().push_back(BuildDataSinkFromOutputTable(outputTable));
    }
    return dataSinkDirectory;
}

NChunkClient::TDataSinkDirectoryPtr BuildDataSinkDirectoryWithAutoMerge(
    const std::vector<TOutputTablePtr>& outputTables,
    const std::vector<bool>& autoMergeEnabled,
    const std::optional<TString>& intermediateAccountName)
{
    auto dataSinkDirectory = New<TDataSinkDirectory>();
    dataSinkDirectory->DataSinks().reserve(outputTables.size());
    YT_VERIFY(ssize(outputTables) == ssize(autoMergeEnabled));
    for (int index = 0; index < ssize(outputTables); ++index) {
        const auto& outputTable = outputTables[index];
        if (autoMergeEnabled[index]) {
            auto& dataSink = dataSinkDirectory->DataSinks().emplace_back();
            dataSink.SetPath(GetIntermediatePath(index));
            dataSink.SetAccount(intermediateAccountName ? intermediateAccountName : outputTable->Account);
        } else {
            dataSinkDirectory->DataSinks().push_back(BuildDataSinkFromOutputTable(outputTable));
        }
    }
    return dataSinkDirectory;
}

std::vector<TInputStreamDescriptorPtr> BuildInputStreamDescriptorsFromOutputStreamDescriptors(
    const std::vector<TOutputStreamDescriptorPtr>& outputStreamDescriptors)
{
    std::vector<TInputStreamDescriptorPtr> inputStreamDescriptors;
    inputStreamDescriptors.reserve(outputStreamDescriptors.size());

    for (const auto& descriptor : outputStreamDescriptors) {
        inputStreamDescriptors.push_back(TInputStreamDescriptor::FromOutputStreamDescriptor(descriptor));
    }

    return inputStreamDescriptors;
}

////////////////////////////////////////////////////////////////////////////////

void TControllerFeatures::AddSingular(TStringBuf name, double value)
{
    Features_[name] += value;
}

void TControllerFeatures::AddSingular(const TString& name, const INodePtr& node)
{
    switch (node->GetType()) {
        case ENodeType::Map:
            for (const auto& [key, child] : node->AsMap()->GetChildren()) {
                AddSingular(name + "." + key, child);
            }
            break;
        case ENodeType::Int64:
            AddSingular(name, node->AsInt64()->GetValue());
            break;
        case ENodeType::Uint64:
            AddSingular(name, node->AsUint64()->GetValue());
            break;
        case ENodeType::Double:
            AddSingular(name, node->AsDouble()->GetValue());
            break;
        case ENodeType::Boolean:
            AddSingular(name, node->AsBoolean()->GetValue());
            break;
        default:
            YT_LOG_FATAL("Unexpected type as controller feature (Type: %v)",
                node->GetType());
            break;
    }
}

void TControllerFeatures::AddCounted(TStringBuf name, double value)
{
    TString sumFeature{name};
    sumFeature += ".sum";
    Features_[sumFeature] += value;
    TString countFeature{name};
    countFeature += ".count";
    Features_[countFeature] += 1;
}

void TControllerFeatures::CalculateJobSatisticsAverage()
{
    static const TString SumSuffix = ".sum";
    static const TString CountSuffix = ".count";
    static const TString AvgSuffix = ".avg";
    static const TString JobStatisticsPrefix = "job_statistics.";
    for (const auto& [sumFeature, sum] : Features_) {
        if (sumFeature.StartsWith(JobStatisticsPrefix) && sumFeature.EndsWith(SumSuffix)) {
            auto feature = sumFeature;
            feature.resize(std::ssize(feature) - std::ssize(SumSuffix));
            auto countFeature = feature + CountSuffix;
            auto avgFeature = feature + AvgSuffix;
            auto it = Features_.find(countFeature);
            if (it != Features_.end() && it->second != 0) {
                Features_[avgFeature] = sum / it->second;
            }
        }
    }
}

void Serialize(const TControllerFeatures& features, IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer).BeginMap()
        .Item("tags").Value(features.Tags_)
        .Item("features").Value(features.Features_)
    .EndMap();
}

NTableClient::TTableReaderOptionsPtr CreateTableReaderOptions(const NScheduler::TJobIOConfigPtr& ioConfig)
{
    auto options = New<NTableClient::TTableReaderOptions>();
    options->EnableRowIndex = ioConfig->ControlAttributes->EnableRowIndex;
    options->EnableTableIndex = ioConfig->ControlAttributes->EnableTableIndex;
    options->EnableRangeIndex = ioConfig->ControlAttributes->EnableRangeIndex;
    options->EnableTabletIndex = ioConfig->ControlAttributes->EnableTabletIndex;
    return options;
}

////////////////////////////////////////////////////////////////////////////////

void UpdateAggregatedJobStatistics(
    TAggregatedJobStatistics& targetStatistics,
    const TJobStatisticsTags& tags,
    const TStatistics& jobStatistics,
    const TStatistics& controllerStatistics,
    int customStatisticsLimit,
    bool* isLimitExceeded)
{
    targetStatistics.AppendStatistics(controllerStatistics, tags);

    if (targetStatistics.CalculateCustomStatisticsCount() > customStatisticsLimit) {
        // Limit is already exceeded, so truncate the statistics.
        auto jobStatisticsCopy = jobStatistics;
        jobStatisticsCopy.RemoveRangeByPrefix("/custom");
        targetStatistics.AppendStatistics(jobStatisticsCopy, tags);
    } else {
        targetStatistics.AppendStatistics(jobStatistics, tags);
    }

    // NB. We need the second check of custom statistics count to ensure that the limit was not
    // violated after the update.
    *isLimitExceeded = targetStatistics.CalculateCustomStatisticsCount() > customStatisticsLimit;
}

void SafeUpdateAggregatedJobStatistics(
    ITaskHost* taskHost,
    TAggregatedJobStatistics& targetStatistics,
    const TJobStatisticsTags& tags,
    const TStatistics& jobStatistics,
    const TStatistics& controllerStatistics,
    int customStatisticsLimit,
    bool* isLimitExceeded)
{
    try {
        UpdateAggregatedJobStatistics(
            targetStatistics,
            tags,
            jobStatistics,
            controllerStatistics,
            customStatisticsLimit,
            isLimitExceeded);
    } catch (const std::exception& ex) {
        taskHost->SetOperationAlert(EOperationAlertType::IncompatibleStatistics, ex);
        // TODO(pavook): fail the operation after setting this alert.
    }
}

////////////////////////////////////////////////////////////////////////////////

TDockerImageSpec::TDockerImageSpec(const TString& dockerImage, const TDockerRegistryConfigPtr& config)
{
    TStringBuf imageTag;

    // Format: [REGISTRY/]IMAGE[:TAG], where REGISTRY is FQDN[:PORT].
    // Registry FQDN must has at least one "." or PORT.
    if (!StringSplitter(dockerImage).Split('/').Limit(2).TryCollectInto(&Registry, &imageTag) ||
        Registry.find_first_of(".:") == TString::npos)
    {
        Registry = "";
        imageTag = dockerImage;
    } else if (Registry == config->InternalRegistryAddress) {
        Registry = "";
    }

    if (!StringSplitter(imageTag).Split(':').Limit(2).TryCollectInto(&Image, &Tag)) {
        Image = imageTag;
        Tag = "latest";
    }
}

bool TDockerImageSpec::IsInternal() const
{
    return Registry.empty();
}

////////////////////////////////////////////////////////////////////////////////

std::vector<TRichYPath> GetLayerPathsFromDockerImage(
    NNative::IClientPtr client,
    const TDockerImageSpec& dockerImage)
{
    try {
        auto tagsPath = TYPath::Join("//", dockerImage.Image, "/_tags");

        auto proxy = NObjectClient::CreateObjectServiceReadProxy(client, EMasterChannelKind::Follower);
        auto req = TYPathProxy::Get(tagsPath);
        auto rspOrError = WaitFor(proxy.Execute(req));

        if (!rspOrError.IsOK()) {
            THROW_ERROR_EXCEPTION("Failed to read tags from %Qv", tagsPath)
                << rspOrError;
        }

        auto rspTags = ConvertToNode(TYsonString(rspOrError.ValueOrThrow()->value()));
        if (!rspTags || rspTags->GetType() != ENodeType::Map) {
            THROW_ERROR_EXCEPTION("Tags document %Qv is not a map", tagsPath);
        }

        auto rspTag = rspTags->AsMap()->FindChild(dockerImage.Tag);
        if (!rspTag) {
            THROW_ERROR_EXCEPTION(
                "No tag %Qv in %Qv, available tags are %v",
                dockerImage.Tag,
                tagsPath,
                rspTags->AsMap()->GetKeys());
        }

        auto layerPaths = ConvertTo<std::vector<TRichYPath>>(rspTag);
        std::reverse(layerPaths.begin(), layerPaths.end());
        return layerPaths;
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION(
            "Failed to load docker image %v:%v",
            dockerImage.Image,
            dockerImage.Tag)
            << ex;
    }
}

////////////////////////////////////////////////////////////////////////////////

IAttributeDictionaryPtr GetNetworkProject(
    NApi::NNative::IClientPtr client,
    TString authenticatedUser,
    TString networkProject)
{
    const auto networkProjectPath = "//sys/network_projects/" + ToYPathLiteral(networkProject);
    auto checkPermissionRsp = WaitFor(client->CheckPermission(authenticatedUser, networkProjectPath, EPermission::Use))
        .ValueOrThrow();
    if (checkPermissionRsp.Action == NSecurityClient::ESecurityAction::Deny) {
        THROW_ERROR_EXCEPTION("User %Qv is not allowed to use network project %Qv",
            authenticatedUser,
            networkProject);
    }

    TGetNodeOptions options{
        .Attributes = TAttributeFilter({"project_id", "enable_nat64", "disable_network"})
    };
    auto networkProjectNode = ConvertToNode(WaitFor(client->GetNode(networkProjectPath, options))
        .ValueOrThrow());
    return networkProjectNode->Attributes().Clone();
}

////////////////////////////////////////////////////////////////////////////////

bool IsStaticTableWithHunks(TInputTablePtr table)
{
    if (!table->Dynamic) {
        bool hunksPresent = AnyOf(table->Schema->Columns(), [] (const TColumnSchema& column) {
            return column.MaxInlineHunkSize();
        });
        if (hunksPresent) {
            return true;
        }
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////

bool HasJobUniquenessRequirements(
    const NScheduler::TOperationSpecBasePtr& operationSpec,
    const std::vector<NScheduler::TUserJobSpecPtr>& userJobSpecs)
{
    return operationSpec->FailOnJobRestart ||
        std::any_of(userJobSpecs.begin(), userJobSpecs.end(), [] (const auto& userJobSpec) {
            return userJobSpec->FailOnJobRestart;
        });
}

////////////////////////////////////////////////////////////////////////////////

template <class TTablePtr>
void FetchTableSchemas(
    const NApi::NNative::IClientPtr& client,
    const std::vector<TTablePtr>& tables,
    TCallback<TTransactionId(const TTablePtr&)> tableToTransactionId,
    bool fetchFromExternalCells)
{
    // The fetchFromExternalCells parameter allows us to choose whether to fetch the schema from native or external cell.
    // Ideally, we want to fetch schemas only from external cells, but it is not possible now. For output
    // tables, lock is acquired after the schema is fetched. This behavior is bad as it may lead to races.
    // Once locking output tables is fixed, we will always fetch the schemas from external cells, and the
    // fetchFromExternalCells parameter will be removed. See also YT-15269.
    // TODO(gepardo): always fetch schemas from external cells.
    auto tableToCellTag = [&] (const TTablePtr& table) {
        return fetchFromExternalCells
            ? table->ExternalCellTag
            : CellTagFromId(table->ObjectId);
    };

    THashMap<TGuid, std::vector<TTablePtr>> schemaIdToTables;
    THashMap<TCellTag, std::vector<TGuid>> cellTagToSchemaIds;
    for (const auto& table : tables) {
        const auto& schemaId = table->SchemaId;
        schemaIdToTables[schemaId].push_back(table);
    }

    for (const auto& [schemaId, tablesWithIdenticalSchema] : schemaIdToTables) {
        YT_VERIFY(!tablesWithIdenticalSchema.empty());
        auto cellTag = tableToCellTag(tablesWithIdenticalSchema.front());
        cellTagToSchemaIds[cellTag].push_back(schemaId);
    }

    std::vector<TFuture<TObjectServiceProxy::TRspExecuteBatchPtr>> asyncResults;
    for (auto& [cellTag, schemaIds] : cellTagToSchemaIds) {
        auto proxy = CreateObjectServiceReadProxy(client, EMasterChannelKind::Follower, cellTag);
        auto batchReq = proxy.ExecuteBatch();

        for (const auto& schemaId : schemaIds) {
            // TODO(gepardo): fetch schema by schema ID directly, without using Get for the corresponding table.
            auto table = schemaIdToTables[schemaId][0];
            auto req = TTableYPathProxy::Get(table->GetObjectIdPath() + "/@schema");
            AddCellTagToSyncWith(req, table->ObjectId);
            SetTransactionId(req, tableToTransactionId(table));
            req->Tag() = schemaId;
            batchReq->AddRequest(req);
        }

        asyncResults.push_back(batchReq->Invoke());
    }

    auto checkError = [] (const auto& error) {
        THROW_ERROR_EXCEPTION_IF_FAILED(error, "Error fetching table schemas");
    };

    auto result = WaitFor(AllSucceeded(asyncResults));
    checkError(result);

    for (const auto& batchRsp : result.Value()) {
        checkError(GetCumulativeError(batchRsp));
        for (const auto& rspOrError : batchRsp->GetResponses<TTableYPathProxy::TRspGet>()) {
            const auto& rsp = rspOrError.Value();
            auto schema = ConvertTo<TTableSchemaPtr>(TYsonString(rsp->value()));
            auto schemaId = std::any_cast<TGuid>(rsp->Tag());
            for (const auto& table : schemaIdToTables[schemaId]) {
                table->Schema = schema;
            }
        }
    }
}

template void FetchTableSchemas(
    const NNative::IClientPtr& client,
    const std::vector<TInputTablePtr>& tables,
    TCallback<TTransactionId(const TInputTablePtr&)> tableToTransactionId,
    bool fetchFromExternalCells);

template void FetchTableSchemas(
    const NNative::IClientPtr& client,
    const std::vector<TOutputTablePtr>& tables,
    TCallback<TTransactionId(const TOutputTablePtr&)> tableToTransactionId,
    bool fetchFromExternalCells);

////////////////////////////////////////////////////////////////////////////////

bool IsBulkInsertAllowedForUser(
    TStringBuf authenticatedUser,
    const IClientPtr& client)
{
    TGetNodeOptions options;
    options.ReadFrom = EMasterChannelKind::Cache;
    options.Attributes = {"enable_bulk_insert"};

    auto path = "//sys/users/" + ToYPathLiteral(authenticatedUser);
    auto rspOrError = WaitFor(client->GetNode(path, options));
    THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Failed to check if bulk insert is enabled");
    auto rsp = ConvertTo<INodePtr>(rspOrError.Value());
    return rsp->Attributes().Get<bool>("enable_bulk_insert", false);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent::NControllers
