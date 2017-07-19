#include "client.h"

#include "batch_request_impl.h"
#include "lock.h"
#include "lock_waiter.h"
#include "mock_client.h"
#include "operation.h"
#include "rpc_parameters_serialization.h"

#include <mapreduce/yt/interface/client.h>

#include <mapreduce/yt/common/log.h>
#include <mapreduce/yt/common/helpers.h>
#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/fluent.h>
#include <mapreduce/yt/common/finally_guard.h>

#include <mapreduce/yt/http/http.h>
#include <mapreduce/yt/http/requests.h>
#include <mapreduce/yt/http/retry_request.h>

#include <mapreduce/yt/io/client_reader.h>
#include <mapreduce/yt/io/client_writer.h>
#include <mapreduce/yt/io/yamr_table_reader.h>
#include <mapreduce/yt/io/yamr_table_writer.h>
#include <mapreduce/yt/io/node_table_reader.h>
#include <mapreduce/yt/io/node_table_writer.h>
#include <mapreduce/yt/io/proto_table_reader.h>
#include <mapreduce/yt/io/proto_table_writer.h>
#include <mapreduce/yt/io/proto_helpers.h>
#include <mapreduce/yt/io/file_reader.h>
#include <mapreduce/yt/io/file_writer.h>
#include <mapreduce/yt/io/block_writer.h>

#include <exception>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

const size_t TClientBase::BUFFER_SIZE = 64 << 20;

////////////////////////////////////////////////////////////////////////////////

class TOperation
    : public IOperation
{
public:
    explicit TOperation(TOperationId id)
        : Id_(std::move(id))
    { }

    virtual const TOperationId& GetId() const override
    {
        return Id_;
    }

private:
    TOperationId Id_;
};

////////////////////////////////////////////////////////////////////////////////

TClientBase::TClientBase(
    const TAuth& auth,
    const TTransactionId& transactionId)
    : Auth_(auth)
    , TransactionId_(transactionId)
{ }

ITransactionPtr TClientBase::StartTransaction(
    const TStartTransactionOptions& options)
{
    return MakeIntrusive<TTransaction>(GetParentClient(), Auth_, TransactionId_, true, options);
}

TNodeId TClientBase::Create(
    const TYPath& path,
    ENodeType type,
    const TCreateOptions& options)
{
    THttpHeader header("POST", "create");
    header.AddMutationId();
    header.SetParameters(NDetail::SerializeParamsForCreate(TransactionId_, path, type, options));
    return ParseGuidFromResponse(RetryRequest(Auth_, header));
}

void TClientBase::Remove(
    const TYPath& path,
    const TRemoveOptions& options)
{
    THttpHeader header("POST", "remove");
    header.AddMutationId();
    header.SetParameters(NDetail::SerializeParamsForRemove(TransactionId_, path, options));
    RetryRequest(Auth_, header);
}

bool TClientBase::Exists(const TYPath& path)
{
    THttpHeader header("GET", "exists");
    header.SetParameters(NDetail::SerializeParamsForExists(TransactionId_, path));
    return ParseBoolFromResponse(RetryRequest(Auth_, header));
}

TNode TClientBase::Get(
    const TYPath& path,
    const TGetOptions& options)
{
    THttpHeader header("GET", "get");
    header.SetParameters(NDetail::SerializeParamsForGet(TransactionId_, path, options));
    return NodeFromYsonString(RetryRequest(Auth_, header));
}

void TClientBase::Set(
    const TYPath& path,
    const TNode& value)
{
    THttpHeader header("PUT", "set");
    header.AddMutationId();
    header.SetParameters(NDetail::SerializeParamsForSet(TransactionId_, path));
    RetryRequest(Auth_, header, NodeToYsonString(value));
}

TNode::TList TClientBase::List(
    const TYPath& path,
    const TListOptions& options)
{
    THttpHeader header("GET", "list");

    TYPath updatedPath = AddPathPrefix(path);
    // FIXME: ugly but quick empty path special case
    // Translate "//" to "/"
    // Translate "//some/constom/prefix/from/config/" to "//some/constom/prefix/from/config"
    if (path.empty() && updatedPath.EndsWith('/')) {
        updatedPath.pop_back();
    }
    header.SetParameters(NDetail::SerializeParamsForList(TransactionId_, updatedPath, options));
    return NodeFromYsonString(RetryRequest(Auth_, header)).AsList();
}

TNodeId TClientBase::Copy(
    const TYPath& sourcePath,
    const TYPath& destinationPath,
    const TCopyOptions& options)
{
    THttpHeader header("POST", "copy");
    header.AddMutationId();
    header.SetParameters(NDetail::SerializeParamsForCopy(TransactionId_, sourcePath, destinationPath, options));
    return ParseGuidFromResponse(RetryRequest(Auth_, header));
}

TNodeId TClientBase::Move(
    const TYPath& sourcePath,
    const TYPath& destinationPath,
    const TMoveOptions& options)
{
    THttpHeader header("POST", "move");
    header.AddMutationId();
    header.SetParameters(NDetail::SerializeParamsForMove(TransactionId_, sourcePath, destinationPath, options));
    return ParseGuidFromResponse(RetryRequest(Auth_, header));
}

TNodeId TClientBase::Link(
    const TYPath& targetPath,
    const TYPath& linkPath,
    const TLinkOptions& options)
{
    THttpHeader header("POST", "link");
    header.AddMutationId();
    header.SetParameters(NDetail::SerializeParamsForLink(TransactionId_, targetPath, linkPath, options));
    return ParseGuidFromResponse(RetryRequest(Auth_, header));
}

void TClientBase::Concatenate(
    const yvector<TYPath>& sourcePaths,
    const TYPath& destinationPath,
    const TConcatenateOptions& options)
{
    THttpHeader header("POST", "concatenate");
    header.AddTransactionId(TransactionId_);
    header.AddMutationId();

    TRichYPath path(AddPathPrefix(destinationPath));
    path.Append(options.Append_);
    header.SetParameters(BuildYsonStringFluently().BeginMap()
        .Item("source_paths").DoListFor(sourcePaths,
            [] (TFluentList fluent, const TYPath& thePath) {
                fluent.Item().Value(AddPathPrefix(thePath));
            })
        .Item("destination_path").Value(path)
    .EndMap());

    RetryRequest(Auth_, header);
}

TRichYPath TClientBase::CanonizeYPath(const TRichYPath& path)
{
    return CanonizePath(Auth_, path);
}

IFileReaderPtr TClientBase::CreateFileReader(
    const TRichYPath& path,
    const TFileReaderOptions& options)
{
    return new TFileReader(
        CanonizePath(Auth_, path),
        Auth_,
        TransactionId_,
        options);
}

IFileWriterPtr TClientBase::CreateFileWriter(
    const TRichYPath& path,
    const TFileWriterOptions& options)
{
    auto realPath = CanonizePath(Auth_, path);
    if (!NYT::Exists(Auth_, TransactionId_, realPath.Path_)) {
        NYT::Create(Auth_, TransactionId_, realPath.Path_, "file");
    }
    return new TFileWriter(realPath, Auth_, TransactionId_, options);
}

TRawTableReaderPtr TClientBase::CreateRawReader(
    const TRichYPath& path,
    EDataStreamFormat format,
    const TTableReaderOptions& options,
    const TString& formatConfig)
{
    return CreateClientReader(path, format, options, formatConfig).Get();
}

TRawTableWriterPtr TClientBase::CreateRawWriter(
    const TRichYPath& path,
    EDataStreamFormat format,
    const TTableWriterOptions& options,
    const TString& formatConfig)
{
    return ::MakeIntrusive<TBlockWriter>(
        Auth_,
        TransactionId_,
        GetWriteTableCommand(),
        format,
        formatConfig,
        CanonizePath(Auth_, path),
        BUFFER_SIZE,
        options).Get();
}

IOperationPtr TClientBase::DoMap(
    const TMapOperationSpec& spec,
    IJob* mapper,
    const TOperationOptions& options)
{
    auto operationId = ExecuteMap(
        Auth_,
        TransactionId_,
        spec,
        mapper,
        options);
    return ::MakeIntrusive<TOperation>(operationId);
}

IOperationPtr TClientBase::DoReduce(
    const TReduceOperationSpec& spec,
    IJob* reducer,
    const TOperationOptions& options)
{
    auto operationId = ExecuteReduce(
        Auth_,
        TransactionId_,
        spec,
        reducer,
        options);
    return ::MakeIntrusive<TOperation>(operationId);
}

IOperationPtr TClientBase::DoJoinReduce(
    const TJoinReduceOperationSpec& spec,
    IJob* reducer,
    const TOperationOptions& options)
{
    auto operationId = ExecuteJoinReduce(
        Auth_,
        TransactionId_,
        spec,
        reducer,
        options);
    return ::MakeIntrusive<TOperation>(operationId);
}

IOperationPtr TClientBase::DoMapReduce(
    const TMapReduceOperationSpec& spec,
    IJob* mapper,
    IJob* reduceCombiner,
    IJob* reducer,
    const TMultiFormatDesc& outputMapperDesc,
    const TMultiFormatDesc& inputReduceCombinerDesc,
    const TMultiFormatDesc& outputReduceCombinerDesc,
    const TMultiFormatDesc& inputReducerDesc,
    const TOperationOptions& options)
{
    auto operationId = ExecuteMapReduce(
        Auth_,
        TransactionId_,
        spec,
        mapper,
        reduceCombiner,
        reducer,
        outputMapperDesc,
        inputReduceCombinerDesc,
        outputReduceCombinerDesc,
        inputReducerDesc,
        options);
    return ::MakeIntrusive<TOperation>(operationId);
}

IOperationPtr TClientBase::Sort(
    const TSortOperationSpec& spec,
    const TOperationOptions& options)
{
    auto operationId = ExecuteSort(
        Auth_,
        TransactionId_,
        spec,
        options);
    return ::MakeIntrusive<TOperation>(operationId);
}

IOperationPtr TClientBase::Merge(
    const TMergeOperationSpec& spec,
    const TOperationOptions& options)
{
    auto operationId = ExecuteMerge(
        Auth_,
        TransactionId_,
        spec,
        options);
    return ::MakeIntrusive<TOperation>(operationId);
}

IOperationPtr TClientBase::Erase(
    const TEraseOperationSpec& spec,
    const TOperationOptions& options)
{
    auto operationId = ExecuteErase(
        Auth_,
        TransactionId_,
        spec,
        options);
    return ::MakeIntrusive<TOperation>(operationId);
}

EOperationStatus TClientBase::CheckOperation(const TOperationId& operationId)
{
    return NYT::CheckOperation(Auth_, TransactionId_, operationId);
}

void TClientBase::AbortOperation(const TOperationId& operationId)
{
    NYT::AbortOperation(Auth_, TransactionId_, operationId);
}

void TClientBase::WaitForOperation(const TOperationId& operationId)
{
    NYT::WaitForOperation(Auth_, TransactionId_, operationId);
}

void TClientBase::AlterTable(
    const TYPath& path,
    const TAlterTableOptions& options)
{
    THttpHeader header("POST", "alter_table");
    header.AddTransactionId(TransactionId_);
    header.AddPath(AddPathPrefix(path));

    if (options.Dynamic_) {
        header.AddParam("dynamic", *options.Dynamic_);
    }
    if (options.Schema_) {
        header.SetParameters(BuildYsonStringFluently().BeginMap()
            .Item("schema")
            .Value(*options.Schema_)
        .EndMap());
    }
    RetryRequest(Auth_, header);
}

::TIntrusivePtr<TClientReader> TClientBase::CreateClientReader(
    const TRichYPath& path,
    EDataStreamFormat format,
    const TTableReaderOptions& options,
    const TString& formatConfig)
{
    return ::MakeIntrusive<TClientReader>(
        CanonizePath(Auth_, path),
        Auth_,
        TransactionId_,
        format,
        formatConfig,
        options);
}

THolder<TClientWriter> TClientBase::CreateClientWriter(
    const TRichYPath& path,
    EDataStreamFormat format,
    const TTableWriterOptions& options,
    const TString& formatConfig)
{
    auto realPath = CanonizePath(Auth_, path);
    if (!NYT::Exists(Auth_, TransactionId_, realPath.Path_)) {
        NYT::Create(Auth_, TransactionId_, realPath.Path_, "table");
    }
    return MakeHolder<TClientWriter>(
        realPath, Auth_, TransactionId_, format, formatConfig, options);
}

::TIntrusivePtr<INodeReaderImpl> TClientBase::CreateNodeReader(
    const TRichYPath& path, const TTableReaderOptions& options)
{
    return new TNodeTableReader(
        CreateClientReader(path, DSF_YSON_BINARY, options), options.SizeLimit_);
}

::TIntrusivePtr<IYaMRReaderImpl> TClientBase::CreateYaMRReader(
    const TRichYPath& path, const TTableReaderOptions& options)
{
    return new TYaMRTableReader(
        CreateClientReader(path, DSF_YAMR_LENVAL, options));
}

::TIntrusivePtr<IProtoReaderImpl> TClientBase::CreateProtoReader(
    const TRichYPath& path,
    const TTableReaderOptions& options,
    const Message* prototype)
{
    yvector<const ::google::protobuf::Descriptor*> descriptors;
    descriptors.push_back(prototype->GetDescriptor());

    if (TConfig::Get()->UseClientProtobuf) {
        return new TProtoTableReader(
            CreateClientReader(path, DSF_YSON_BINARY, options),
            std::move(descriptors));
    } else {
        auto formatConfig = NodeToYsonString(MakeProtoFormatConfig(prototype));
        return new TLenvalProtoTableReader(
            CreateClientReader(path, DSF_PROTO, options, formatConfig),
            std::move(descriptors));
    }
}

::TIntrusivePtr<INodeWriterImpl> TClientBase::CreateNodeWriter(
    const TRichYPath& path, const TTableWriterOptions& options)
{
    return new TNodeTableWriter(
        CreateClientWriter(path, DSF_YSON_BINARY, options));
}

::TIntrusivePtr<IYaMRWriterImpl> TClientBase::CreateYaMRWriter(
    const TRichYPath& path, const TTableWriterOptions& options)
{
    return new TYaMRTableWriter(
        CreateClientWriter(path, DSF_YAMR_LENVAL, options));
}

::TIntrusivePtr<IProtoWriterImpl> TClientBase::CreateProtoWriter(
    const TRichYPath& path,
    const TTableWriterOptions& options,
    const Message* prototype)
{
    yvector<const ::google::protobuf::Descriptor*> descriptors;
    descriptors.push_back(prototype->GetDescriptor());

    if (TConfig::Get()->UseClientProtobuf) {
        return new TProtoTableWriter(
            CreateClientWriter(path, DSF_YSON_BINARY, options),
            std::move(descriptors));
    } else {
        auto formatConfig = NodeToYsonString(MakeProtoFormatConfig(prototype));
        return new TLenvalProtoTableWriter(
            CreateClientWriter(path, DSF_PROTO, options, formatConfig),
            std::move(descriptors));
    }
}

////////////////////////////////////////////////////////////////////////////////

TTransaction::TTransaction(
    TClientPtr parentClient,
    const TAuth& auth,
    const TTransactionId& transactionId,
    bool isOwning,
    const TStartTransactionOptions& options)
    : TClientBase(auth, transactionId)
    , PingableTx_(isOwning ?
        new TPingableTransaction(
            auth,
            transactionId, // parent id
            options.Timeout_,
            options.PingAncestors_,
            options.Title_,
            options.Attributes_)
        : nullptr)
    , ParentClient_(parentClient)
{
    TransactionId_ = isOwning ? PingableTx_->GetId() : transactionId;
}

const TTransactionId& TTransaction::GetId() const
{
    return TransactionId_;
}

ILockPtr TTransaction::Lock(
    const TYPath& path,
    ELockMode mode,
    const TLockOptions& options)
{
    THttpHeader header("POST", "lock");
    header.AddMutationId();
    header.SetParameters(NDetail::SerializeParamsForLock(TransactionId_, path, mode, options));

    auto lockId = ParseGuidFromResponse(RetryRequest(Auth_, header));
    if (options.Waitable_) {
        return ::MakeIntrusive<TLock>(lockId, GetParentClient());
    } else {
        return ::MakeIntrusive<TLock>(lockId);
    }
}

void TTransaction::Commit()
{
    if (PingableTx_) {
        PingableTx_->Commit();
    } else {
        CommitTransaction(Auth_, TransactionId_);
    }
}

void TTransaction::Abort()
{
    if (PingableTx_) {
        PingableTx_->Abort();
    } else {
        AbortTransaction(Auth_, TransactionId_);
    }
}

TClientPtr TTransaction::GetParentClient()
{
    return ParentClient_;
}

////////////////////////////////////////////////////////////////////////////////

TClient::TClient(
    const TAuth& auth,
    const TTransactionId& globalId)
    : TClientBase(auth, globalId)
{ }

TClient::~TClient() = default;

ITransactionPtr TClient::AttachTransaction(
    const TTransactionId& transactionId)
{
    return MakeIntrusive<TTransaction>(this, Auth_, transactionId, false, TStartTransactionOptions());
}

void TClient::MountTable(
    const TYPath& path,
    const TMountTableOptions& options)
{
    THttpHeader header("POST", "mount_table");
    SetTabletParams(header, path, options);
    if (options.CellId_) {
        header.AddParam("cell_id", GetGuidAsString(*options.CellId_));
    }
    header.AddParam("freeze", options.Freeze_);
    RetryRequest(Auth_, header);
}

void TClient::UnmountTable(
    const TYPath& path,
    const TUnmountTableOptions& options)
{
    THttpHeader header("POST", "unmount_table");
    SetTabletParams(header, path, options);
    header.AddParam("force", options.Force_);
    RetryRequest(Auth_, header);
}

void TClient::RemountTable(
    const TYPath& path,
    const TRemountTableOptions& options)
{
    THttpHeader header("POST", "remount_table");
    SetTabletParams(header, path, options);
    RetryRequest(Auth_, header);
}

void TClient::FreezeTable(
    const TYPath& path,
    const TFreezeTableOptions& options)
{
    THttpHeader header("POST", "freeze_table");
    SetTabletParams(header, path, options);
    RetryRequest(Auth_, header);
}

void TClient::UnfreezeTable(
    const TYPath& path,
    const TUnfreezeTableOptions& options)
{
    THttpHeader header("POST", "unfreeze_table");
    SetTabletParams(header, path, options);
    RetryRequest(Auth_, header);
}

void TClient::ReshardTable(
    const TYPath& path,
    const yvector<TKey>& keys,
    const TReshardTableOptions& options)
{
    THttpHeader header("POST", "reshard_table");
    SetTabletParams(header, path, options);
    header.SetParameters(BuildYsonStringFluently().BeginMap()
        .Item("pivot_keys").List(keys)
    .EndMap());
    RetryRequest(Auth_, header);
}

void TClient::ReshardTable(
    const TYPath& path,
    i32 tabletCount,
    const TReshardTableOptions& options)
{
    THttpHeader header("POST", "reshard_table");
    SetTabletParams(header, path, options);
    header.AddParam("tablet_count", static_cast<i64>(tabletCount));
    RetryRequest(Auth_, header);
}

void TClient::InsertRows(
    const TYPath& path,
    const TNode::TList& rows,
    const TInsertRowsOptions& options)
{
    THttpHeader header("PUT", "insert_rows");
    header.SetDataStreamFormat(DSF_YSON_BINARY);
    header.SetParameters(NDetail::SerializeParametersForInsertRows(path, options));

    auto body = NodeListToYsonString(rows);
    RetryRequest(Auth_, header, body, true);
}

void TClient::DeleteRows(
    const TYPath& path,
    const TNode::TList& keys,
    const TDeleteRowsOptions& options)
{
    THttpHeader header("PUT", "delete_rows");
    header.SetDataStreamFormat(DSF_YSON_BINARY);
    header.SetParameters(NDetail::SerializeParametersForDeleteRows(path, options));

    auto body = NodeListToYsonString(keys);
    RetryRequest(Auth_, header, body, true);
}

TNode::TList TClient::LookupRows(
    const TYPath& path,
    const TNode::TList& keys,
    const TLookupRowsOptions& options)
{
    Y_UNUSED(options);
    THttpHeader header("PUT", "lookup_rows");
    header.AddPath(AddPathPrefix(path));
    header.SetDataStreamFormat(DSF_YSON_BINARY);

    header.SetParameters(BuildYsonStringFluently().BeginMap()
        .DoIf(options.Timeout_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("timeout").Value(static_cast<i64>(options.Timeout_->MilliSeconds()));
        })
        .Item("keep_missing_rows").Value(options.KeepMissingRows_)
        .DoIf(options.Columns_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("column_names").Value(*options.Columns_);
        })
    .EndMap());

    auto body = NodeListToYsonString(keys);
    auto response = RetryRequest(Auth_, header, body, true);
    return NodeFromYsonString(response, YT_LIST_FRAGMENT).AsList();
}

TNode::TList TClient::SelectRows(
    const TString& query,
    const TSelectRowsOptions& options)
{
    THttpHeader header("GET", "select_rows");
    header.SetDataStreamFormat(DSF_YSON_BINARY);

    header.SetParameters(BuildYsonStringFluently().BeginMap()
        .Item("query").Value(query)
        .DoIf(options.Timeout_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("timeout").Value(static_cast<i64>(options.Timeout_->MilliSeconds()));
        })
        .DoIf(options.InputRowLimit_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("input_row_limit").Value(*options.InputRowLimit_);
        })
        .DoIf(options.OutputRowLimit_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("output_row_limit").Value(*options.OutputRowLimit_);
        })
        .Item("range_expansion_limit").Value(options.RangeExpansionLimit_)
        .Item("fail_on_incomplete_result").Value(options.FailOnIncompleteResult_)
        .Item("verbose_logging").Value(options.VerboseLogging_)
        .Item("enable_code_cache").Value(options.EnableCodeCache_)
    .EndMap());

    auto response = RetryRequest(Auth_, header, "", true);
    return NodeFromYsonString(response, YT_LIST_FRAGMENT).AsList();
}

void TClient::EnableTableReplica(const TReplicaId& replicaid)
{
    THttpHeader header("POST", "enable_table_replica");
    header.AddParam("replica_id", GetGuidAsString(replicaid));
    RetryRequest(Auth_, header);
}

void TClient::DisableTableReplica(const TReplicaId& replicaid)
{
    THttpHeader header("POST", "disable_table_replica");
    header.AddParam("replica_id", GetGuidAsString(replicaid));
    RetryRequest(Auth_, header);
}

ui64 TClient::GenerateTimestamp()
{
    THttpHeader header("GET", "generate_timestamp");
    auto response = RetryRequest(Auth_, header, "", true);
    return NodeFromYsonString(response).AsUint64();
}

void TClient::ExecuteBatch(const TBatchRequest& request, const TExecuteBatchOptions& options)
{
    if (request.Impl_->IsExecuted()) {
        ythrow yexception() << "Cannot execute batch request since it is alredy executed";
    }
    NDetail::TFinallyGuard g([&] {
        request.Impl_->MarkExecuted();
    });

    NDetail::TAttemptLimitedRetryPolicy retryPolicy(TConfig::Get()->RetryCount);

    const auto concurrency = options.Concurrency_.GetOrElse(50);
    const auto batchPartMaxSize = options.BatchPartMaxSize_.GetOrElse(concurrency * 5);

    while (request.Impl_->BatchSize()) {
        NDetail::TBatchRequestImpl retryBatch;

        while (request.Impl_->BatchSize()) {
            auto parameters = TNode::CreateMap();
            TInstant nextTry;
            request.Impl_->FillParameterList(batchPartMaxSize, &parameters["requests"], &nextTry);
            if (nextTry) {
                SleepUntil(nextTry);
            }
            parameters["concurrency"] = concurrency;
            auto body = NodeToYsonString(parameters);
            THttpHeader header("POST", "execute_batch");
            header.AddMutationId();
            NDetail::TResponseInfo result;
            try {
                result = RetryRequest(Auth_, header, body, retryPolicy);
            } catch (const yexception& e) {
                request.Impl_->SetErrorResult(std::current_exception());
                retryBatch.SetErrorResult(std::current_exception());
                throw;
            }
            request.Impl_->ParseResponse(std::move(result), retryPolicy, &retryBatch, this);
        }

        *request.Impl_ = std::move(retryBatch);
    }
}

TLockWaiter& TClient::GetLockWaiter()
{

    if (!LockWaiter_) {
        // We don't use current clinet and create new client because LockWaiter might use
        // this client during current client shutdown.
        // That might lead to incrementing of current client refcount and double delete of current client object.
        LockWaiter_ = MakeHolder<TLockWaiter>(Clone());
    }
    return *LockWaiter_;
}

IClientPtr TClient::Clone()
{
    return MakeIntrusive<TClient>(Auth_, TransactionId_);
}

TClientPtr TClient::GetParentClient()
{
    return this;
}

template <class TOptions>
void TClient::SetTabletParams(
    THttpHeader& header,
    const TYPath& path,
    const TOptions& options)
{
    header.AddPath(AddPathPrefix(path));
    if (options.FirstTabletIndex_) {
        header.AddParam("first_tablet_index", *options.FirstTabletIndex_);
    }
    if (options.LastTabletIndex_) {
        header.AddParam("last_tablet_index", *options.LastTabletIndex_);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

IClientPtr CreateClient(
    const TString& serverName,
    const TCreateClientOptions& options)
{
    bool mockRun = getenv("YT_CLIENT_MOCK_RUN") ? FromString<bool>(getenv("YT_CLIENT_MOCK_RUN")) : false;
    if (mockRun) {
        LOG_INFO("Running client in mock regime");
        return new TMockClient();
    }

    auto globalTxId = GetGuid(TConfig::Get()->GlobalTxId);

    TAuth auth;
    auth.ServerName = serverName;
    if (serverName.find('.') == TString::npos &&
        serverName.find(':') == TString::npos)
    {
        auth.ServerName += ".yt.yandex.net";
    }

    auth.Token = TConfig::Get()->Token;
    if (options.Token_) {
        auth.Token = options.Token_;
    } else if (options.TokenPath_) {
        auth.Token = TConfig::LoadTokenFromFile(options.TokenPath_);
    }
    TConfig::ValidateToken(auth.Token);

    return new NDetail::TClient(auth, globalTxId);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
