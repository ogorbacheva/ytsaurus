#include "table_reader.h"
#include "private.h"
#include "transaction.h"
#include "connection.h"

#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/ytlib/chunk_client/chunk_reader_statistics.h>
#include <yt/ytlib/chunk_client/chunk_spec.h>
#include <yt/ytlib/chunk_client/data_source.h>
#include <yt/ytlib/chunk_client/dispatcher.h>
#include <yt/ytlib/chunk_client/multi_reader_base.h>
#include <yt/ytlib/chunk_client/helpers.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/client/api/table_reader.h>

#include <yt/client/node_tracker_client/node_directory.h>

#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/client/chunk_client/chunk_replica.h>

#include <yt/client/object_client/helpers.h>

#include <yt/ytlib/table_client/config.h>
#include <yt/ytlib/table_client/blob_table_writer.h>
#include <yt/ytlib/table_client/chunk_meta_extensions.h>
#include <yt/ytlib/table_client/helpers.h>
#include <yt/ytlib/table_client/schemaless_chunk_reader.h>
#include <yt/ytlib/table_client/table_ypath_proxy.h>

#include <yt/ytlib/transaction_client/helpers.h>
#include <yt/ytlib/transaction_client/transaction_listener.h>

#include <yt/client/table_client/name_table.h>

#include <yt/client/ypath/rich.h>

#include <yt/core/concurrency/scheduler.h>
#include <yt/core/concurrency/throughput_throttler.h>
#include <yt/core/concurrency/async_stream.h>

#include <yt/core/misc/protobuf_helpers.h>
#include <yt/core/misc/range.h>

#include <yt/core/rpc/public.h>

#include <yt/core/ytree/ypath_proxy.h>

namespace NYT::NApi::NNative {

using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NNodeTrackerClient;
using namespace NObjectClient;
using namespace NTableClient;
using namespace NApi;
using namespace NTransactionClient;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;
using namespace NRpc;

using NChunkClient::TChunkReaderStatistics;
using NChunkClient::TDataSliceDescriptor;
using NYT::TRange;

////////////////////////////////////////////////////////////////////////////////

class TTableReader
    : public ITableReader
    , public TTransactionListener
{
public:
    TTableReader(
        TTableReaderConfigPtr config,
        TTableReaderOptions options,
        IClientPtr client,
        NApi::ITransactionPtr transaction,
        const TRichYPath& richPath,
        TNameTablePtr nameTable,
        const TColumnFilter& columnFilter,
        bool unordered,
        IThroughputThrottlerPtr bandwidthThrottler,
        IThroughputThrottlerPtr rpsThrottler)
        : Config_(std::move(config))
        , Options_(std::move(options))
        , Client_(std::move(client))
        , Transaction_(std::move(transaction))
        , RichPath_(richPath)
        , NameTable_(std::move(nameTable))
        , ColumnFilter_(columnFilter)
        , BandwidthThrottler_(std::move(bandwidthThrottler))
        , RpsThrottler_(std::move(rpsThrottler))
        , TransactionId_(Transaction_ ? Transaction_->GetId() : NullTransactionId)
    {
        YCHECK(Config_);
        YCHECK(Client_);

        ReadyEvent_ = BIND(&TTableReader::DoOpen, MakeStrong(this))
            .AsyncVia(NChunkClient::TDispatcher::Get()->GetReaderInvoker())
            .Run();
    }

    virtual bool Read(std::vector<TUnversionedRow>* rows) override
    {
        if (ReadDeadline_) {
            if (TInstant::Now() >= ReadDeadline_) {
                THROW_ERROR_EXCEPTION(NTableClient::EErrorCode::ReaderDeadlineExpired, "Reader deadline expired");
            }
        }

        rows->clear();

        if (IsAborted()) {
            return true;
        }

        if (!ReadyEvent_.IsSet() || !ReadyEvent_.Get().IsOK()) {
            return true;
        }

        YCHECK(UnderlyingReader_);
        return UnderlyingReader_->Read(rows);
    }

    virtual TFuture<void> GetReadyEvent() override
    {
        if (!ReadyEvent_.IsSet() || !ReadyEvent_.Get().IsOK()) {
            return ReadyEvent_;
        }

        if (IsAborted()) {
            return MakeFuture(GetAbortError());
        }

        YCHECK(UnderlyingReader_);
        return UnderlyingReader_->GetReadyEvent();
    }

    i64 GetTableRowIndex() const
    {
        YCHECK(UnderlyingReader_);
        return UnderlyingReader_->GetTableRowIndex();
    }

    virtual i64 GetTotalRowCount() const override
    {
        YCHECK(UnderlyingReader_);
        return UnderlyingReader_->GetTotalRowCount();
    }

    virtual NChunkClient::NProto::TDataStatistics GetDataStatistics() const override
    {
        YCHECK(UnderlyingReader_);
        return UnderlyingReader_->GetDataStatistics();
    }

    virtual const TNameTablePtr& GetNameTable() const override
    {
        YCHECK(UnderlyingReader_);
        return UnderlyingReader_->GetNameTable();
    }

    virtual TKeyColumns GetKeyColumns() const override
    {
        YCHECK(UnderlyingReader_);
        return UnderlyingReader_->GetKeyColumns();
    }

private:
    const TTableReaderConfigPtr Config_;
    TTableReaderOptions Options_;
    const IClientPtr Client_;
    const NApi::ITransactionPtr Transaction_;
    const TRichYPath RichPath_;
    const TNameTablePtr NameTable_;
    const TColumnFilter ColumnFilter_;
    const IThroughputThrottlerPtr BandwidthThrottler_;
    const IThroughputThrottlerPtr RpsThrottler_;
    const TTransactionId TransactionId_;
    const TNodeDirectoryPtr NodeDirectory_;

    TClientBlockReadOptions BlockReadOptions_;

    TFuture<void> ReadyEvent_;

    ISchemalessMultiChunkReaderPtr UnderlyingReader_;

    NLogging::TLogger Logger = ApiLogger;

    TInstant ReadDeadline_;

    void DoOpen()
    {
        UnderlyingReader_ = WaitFor(CreateSchemalessMultiChunkReader(
            Client_,
            RichPath_,
            Options_,
            NameTable_,
            ColumnFilter_,
            BandwidthThrottler_,
            RpsThrottler_))
            .ValueOrThrow();

        if (Transaction_) {
            StartListenTransaction(Transaction_);
        }

        if (Config_->MaxReadDuration) {
            ReadDeadline_ = TInstant::Now() + *Config_->MaxReadDuration;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TFuture<ITableReaderPtr> CreateTableReader(
    IClientPtr client,
    const NYPath::TRichYPath& path,
    const TTableReaderOptions& options,
    TNameTablePtr nameTable,
    const TColumnFilter& columnFilter,
    IThroughputThrottlerPtr bandwidthThrottler,
    IThroughputThrottlerPtr rpsThrottler)
{
    NApi::ITransactionPtr transaction;
    if (options.TransactionId) {
        TTransactionAttachOptions transactionOptions;
        transactionOptions.Ping = options.Ping;
        transactionOptions.PingAncestors = options.PingAncestors;
        transaction = client->AttachTransaction(options.TransactionId, transactionOptions);
    }

    auto reader = New<TTableReader>(
        options.Config ? options.Config : New<TTableReaderConfig>(),
        options,
        client,
        transaction,
        path,
        nameTable,
        columnFilter,
        options.Unordered,
        bandwidthThrottler,
        rpsThrottler);

    return reader->GetReadyEvent().Apply(BIND([=] () -> ITableReaderPtr {
        return reader;
    }));
}

////////////////////////////////////////////////////////////////////////////////

TFuture<ISchemalessMultiChunkReaderPtr> CreateSchemalessMultiChunkReader(
    IClientPtr client,
    const NYPath::TRichYPath& richPath,
    const TTableReaderOptions& options,
    TNameTablePtr nameTable,
    const TColumnFilter& columnFilter,
    NConcurrency::IThroughputThrottlerPtr bandwidthThrottler,
    NConcurrency::IThroughputThrottlerPtr rpsThrottler)
{
    auto Logger = ApiLogger;

    const auto& path = richPath.GetPath();
    auto readSessionId = TReadSessionId::Create();
    Logger.AddTag("Path: %v, TransactionId: %v, ReadSessionId: %v",
        path,
        options.TransactionId,
        readSessionId);

    YT_LOG_INFO("Opening table reader");

    TUserObject userObject;
    userObject.Path = path;

    auto config = options.Config ? options.Config : New<TTableReaderConfig>();

    GetUserObjectBasicAttributes(
        client,
        TMutableRange<TUserObject>(&userObject, 1),
        options.TransactionId,
        Logger,
        EPermission::Read,
        config->SuppressAccessTracking);

    const auto& objectId = userObject.ObjectId;
    const auto tableCellTag = userObject.CellTag;

    TYPath objectIdPath;
    if (objectId) {
        objectIdPath = FromObjectId(objectId);
        if (userObject.Type != EObjectType::Table) {
            THROW_ERROR_EXCEPTION("Invalid type of %v: expected %Qlv, actual %Qlv",
                path,
                EObjectType::Table,
                userObject.Type);
        }
    } else {
        YT_LOG_INFO("Table is virtual, performing further operations with its original path rather with its object id");
        objectIdPath = path;
    }

    int chunkCount;
    bool dynamic;
    TTableSchema schema;
    auto timestamp = richPath.GetTimestamp();

    {
        YT_LOG_INFO("Requesting table schema");

        auto channel = client->GetMasterChannelOrThrow(
            EMasterChannelKind::Follower,
            CellTagFromId(objectId));

        TObjectServiceProxy proxy(channel);

        auto req = TYPathProxy::Get(objectIdPath + "/@");
        SetTransactionId(req, options.TransactionId);
        SetSuppressAccessTracking(req, config->SuppressAccessTracking);
        std::vector<TString> attributeKeys{
            "chunk_count",
            "dynamic",
            "retained_timestamp",
            "schema",
            "unflushed_timestamp"
        };
        ToProto(req->mutable_attributes()->mutable_keys(), attributeKeys);

        auto rspOrError = WaitFor(proxy.Execute(req));
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error getting table schema %v",
            path);

        const auto& rsp = rspOrError.Value();
        auto attributes = ConvertToAttributes(TYsonString(rsp->value()));

        chunkCount = attributes->Get<int>("chunk_count");
        dynamic = attributes->Get<bool>("dynamic");
        schema = attributes->Get<TTableSchema>("schema");

        // Validate that timestamp is correct.
        ValidateDynamicTableTimestamp(richPath, dynamic, schema, *attributes);
    }

    std::vector<TChunkSpec> chunkSpecs;

    {
        YT_LOG_INFO("Fetching table chunks");

        FetchChunkSpecs(
            client,
            client->GetNativeConnection()->GetNodeDirectory(),
            tableCellTag,
            objectIdPath,
            richPath.GetRanges(),
            chunkCount,
            config->MaxChunksPerFetch,
            config->MaxChunksPerLocateRequest,
            [&] (TChunkOwnerYPathProxy::TReqFetchPtr req) {
                req->set_fetch_all_meta_extensions(false);
                req->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
                req->add_extension_tags(TProtoExtensionTag<NTableClient::NProto::TBoundaryKeysExt>::Value);
                req->set_fetch_parity_replicas(config->EnableAutoRepair);
                SetTransactionId(req, options.TransactionId);
                SetSuppressAccessTracking(req, config->SuppressAccessTracking);
            },
            Logger,
            &chunkSpecs,
            config->UnavailableChunkStrategy == EUnavailableChunkStrategy::Skip /* skipUnavailableChunks */);

        CheckUnavailableChunks(config->UnavailableChunkStrategy, &chunkSpecs);
    }

    // TODO(max42): looks strange, maybe use table reader options that are passed as an argument?
    auto internalOptions = New<NTableClient::TTableReaderOptions>();
    internalOptions->EnableTableIndex = true;
    internalOptions->EnableRangeIndex = true;
    internalOptions->EnableRowIndex = true;

    TClientBlockReadOptions blockReadOptions;
    blockReadOptions.WorkloadDescriptor = config->WorkloadDescriptor;
    blockReadOptions.WorkloadDescriptor.Annotations.push_back(Format("TablePath: %v", path));
    blockReadOptions.ChunkReaderStatistics = New<TChunkReaderStatistics>();
    blockReadOptions.ReadSessionId = readSessionId;

    ISchemalessMultiChunkReaderPtr reader;

    auto dataSourceDirectory = New<NChunkClient::TDataSourceDirectory>();
    if (dynamic && schema.IsSorted()) {
        dataSourceDirectory->DataSources().push_back(MakeVersionedDataSource(
            path,
            schema,
            richPath.GetColumns(),
            timestamp.value_or(AsyncLastCommittedTimestamp)));

        auto dataSliceDescriptor = TDataSliceDescriptor(std::move(chunkSpecs));

        const auto& dataSource = dataSourceDirectory->DataSources()[dataSliceDescriptor.GetDataSourceIndex()];
        auto adjustedColumnFilter = columnFilter.IsUniversal()
            ? CreateColumnFilter(dataSource.Columns(), nameTable)
            : columnFilter;

        reader = CreateSchemalessMergingMultiChunkReader(
            config,
            internalOptions,
            client,
            // HTTP proxy doesn't have a node descriptor.
            TNodeDescriptor(),
            std::nullopt,
            client->GetNativeConnection()->GetBlockCache(),
            client->GetNativeConnection()->GetNodeDirectory(),
            dataSourceDirectory,
            dataSliceDescriptor,
            nameTable,
            blockReadOptions,
            adjustedColumnFilter,
            /* trafficMeter */ nullptr,
            bandwidthThrottler,
            rpsThrottler);
    } else {
        dataSourceDirectory->DataSources().push_back(MakeUnversionedDataSource(
            path,
            schema,
            richPath.GetColumns()));

        std::vector<TDataSliceDescriptor> dataSliceDescriptors;
        for (auto& chunkSpec : chunkSpecs) {
            dataSliceDescriptors.emplace_back(chunkSpec);
        }

        auto factory = options.Unordered
            ? CreateSchemalessParallelMultiReader
            : CreateSchemalessSequentialMultiReader;
        reader = factory(
            config,
            internalOptions,
            client,
            // HTTP proxy doesn't have a node descriptor.
            TNodeDescriptor(),
            std::nullopt,
            client->GetNativeConnection()->GetBlockCache(),
            client->GetNativeConnection()->GetNodeDirectory(),
            dataSourceDirectory,
            std::move(dataSliceDescriptors),
            nameTable,
            blockReadOptions,
            columnFilter,
            schema.GetKeyColumns(),
            /* partitionTag */ std::nullopt,
            /* trafficMeter */ nullptr,
            bandwidthThrottler,
            rpsThrottler);
    }

    return reader->GetReadyEvent()
        .Apply(BIND([=] {
            return reader;
        }));
}

} // namespace NYT::NApi::NNative

