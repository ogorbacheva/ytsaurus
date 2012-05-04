#include "arguments.h"
#include "preprocess.h"

#include <build.h>

#include <ytlib/misc/home.h>
#include <ytlib/misc/fs.h>

#include <ytlib/ytree/tokenizer.h>

#include <ytlib/job_proxy/config.h>

#include <ytlib/driver/driver.h>
#include <ytlib/driver/command.h>

#include <ytlib/logging/log_manager.h>

#include <ytlib/cypress/cypress_service_proxy.h>

#include <ytlib/scheduler/scheduler_proxy.h>
#include <ytlib/scheduler/helpers.h>

#include <util/folder/dirut.h>

namespace NYT {

using namespace NYTree;
using namespace NScheduler;
using namespace NDriver;
using namespace NCypress;
using namespace NRpc;

static const char* UserConfigFileName = ".ytdriver.conf";
static const char* SystemConfigFileName = "ytdriver.conf";
static const char* SystemConfigPath = "/etc/";

////////////////////////////////////////////////////////////////////////////////

class TArgsParserBase::TPassthroughDriverHost
    : public IDriverHost
{
public:
    TPassthroughDriverHost()
        : InputStream(new TBufferedInput(&StdInStream()))
        , OutputStream(new TBufferedOutput(&StdOutStream()))
        , ErrorStream(new TBufferedOutput(&StdErrStream()))
    { }

private:
    virtual TSharedPtr<TInputStream> GetInputStream()
    {
        return InputStream;
    }

    virtual TSharedPtr<TOutputStream> GetOutputStream()
    {
        return OutputStream;
    }

    virtual TSharedPtr<TOutputStream> GetErrorStream()
    {
        return ErrorStream;
    }

    TSharedPtr<TInputStream> InputStream;
    TSharedPtr<TOutputStream> OutputStream;
    TSharedPtr<TOutputStream> ErrorStream;
};

////////////////////////////////////////////////////////////////////////////////

class TArgsParserBase::TInterceptingDriverHost
    : public IDriverHost
{
public:
    TInterceptingDriverHost(const TYson& input = "")
        : Input(input)
        , InputStream(new TStringInput(Input))
        , OutputStream(new TStringOutput(Output))
        , ErrorStream(new TStringOutput(Error))
    { }

    const TYson& GetInput()
    {
        return Input;
    }

    const TYson& GetOutput()
    {
        return Output;
    }
    
    const TYson& GetError()
    {
        return Error;
    }

private:
    virtual TSharedPtr<TInputStream> GetInputStream()
    {
        return InputStream;
    }

    virtual TSharedPtr<TOutputStream> GetOutputStream()
    {
        return OutputStream;
    }

    virtual TSharedPtr<TOutputStream> GetErrorStream()
    {
        return ErrorStream;
    }

    TYson Input;
    TYson Output;
    TYson Error;

    TSharedPtr<TInputStream> InputStream;
    TSharedPtr<TOutputStream> OutputStream;
    TSharedPtr<TOutputStream> ErrorStream;
};

////////////////////////////////////////////////////////////////////////////////

TArgsParserBase::TArgsParserBase()
    : CmdLine("Command line", ' ', YT_VERSION)
    , ConfigArg("", "config", "configuration file", false, "", "file_name")
    , OutputFormatArg("", "format", "output format", false, TFormat(), "text, pretty, binary")
    , ConfigSetArg("", "config_set", "set configuration value", false, "ypath=yson")
    , OptsArg("", "opts", "other options", false, "key=yson")
{
    CmdLine.add(ConfigArg);
    CmdLine.add(OptsArg);
    CmdLine.add(OutputFormatArg);
    CmdLine.add(ConfigSetArg);
}

INodePtr TArgsParserBase::ParseArgs(const std::vector<std::string>& args)
{
    auto argsCopy = args;
    CmdLine.parse(argsCopy);

    auto builder = CreateBuilderFromFactory(GetEphemeralNodeFactory());
    builder->BeginTree();
    builder->OnBeginMap();
    BuildRequest(~builder);
    builder->OnEndMap();
    return builder->EndTree();
}

TArgsParserBase::TConfig::TPtr TArgsParserBase::ParseConfig()
{
    Stroka configFromCmd = ConfigArg.getValue();;
    Stroka configFromEnv = Stroka(getenv("YT_CONFIG"));
    Stroka userConfig = NFS::CombinePaths(GetHomePath(), UserConfigFileName);
    Stroka systemConfig = NFS::CombinePaths(SystemConfigPath, SystemConfigFileName);

    auto configName = configFromCmd;
    if (configName.empty()) {
        configName = configFromEnv;
        if (configName.empty()) {
            configName = userConfig;
            if (!isexist(~configName)) {
                configName = systemConfig;
                if (!isexist(~configName)) {
                    ythrow yexception() <<
                        Sprintf("Config wasn't found. Please specify it using on of the following:\n"
                        "commandline option --config\n"
                        "env YT_CONFIG\n"
                        "user file: %s\n"
                        "system file: %s",
                        ~userConfig, ~systemConfig);
                }
            }
        }
    }

    INodePtr configNode;
    try {
        TIFStream configStream(configName);
        configNode = DeserializeFromYson(&configStream);
    } catch (const std::exception& ex) {
        ythrow yexception() << Sprintf("Error reading configuration\n%s", ex.what());
    }

    ApplyConfigUpdates(configNode);   

    auto config = New<TConfig>();
    try {
        config->Load(~configNode);
    } catch (const std::exception& ex) {
        ythrow yexception() << Sprintf("Error parsing configuration\n%s", ex.what());
    }

    NLog::TLogManager::Get()->Configure(~config->Logging);

    return config;
}

TError TArgsParserBase::Execute(const std::vector<std::string>& args)
{
    auto request = ParseArgs(args);
    auto config = ParseConfig();
    TPassthroughDriverHost driverHost;
    auto driver = CreateDriver(config, &driverHost);
    return driver->Execute(GetDriverCommandName(), request);
}

TArgsParserBase::TFormat TArgsParserBase::GetOutputFormat()
{
    return OutputFormatArg.getValue();
}

void TArgsParserBase::ApplyConfigUpdates(IYPathServicePtr service)
{
    FOREACH (auto updateString, ConfigSetArg.getValue()) {
        TTokenizer tokenizer(updateString);
        tokenizer.ParseNext();
        while (tokenizer.GetCurrentType() != ETokenType::Equals) {
            if (!tokenizer.ParseNext()) {
                ythrow yexception() << "Incorrect option";
            }
        }
        TStringBuf ypath = TStringBuf(updateString).Chop(tokenizer.GetCurrentInput().length());
        SyncYPathSet(service, TYPath(ypath), TYson(tokenizer.GetCurrentSuffix()));
    }
}

void TArgsParserBase::BuildOptions(IYsonConsumer* consumer)
{
    // TODO(babenko): think about a better way of doing this
    FOREACH (const auto& opts, OptsArg.getValue()) {
        TYson yson = Stroka("{") + Stroka(opts) + "}";
        auto items = DeserializeFromYson(yson)->AsMap();
        FOREACH (const auto& pair, items->GetChildren()) {
            consumer->OnKeyedItem(pair.first);
            VisitTree(pair.second, consumer, true);
        }
    }
}

void TArgsParserBase::BuildRequest(IYsonConsumer* consumer)
{
    UNUSED(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TTransactedArgsParser::TTransactedArgsParser()
    : TxArg("", "tx", "set transaction id", false, "", "transaction_id")
{
    CmdLine.add(TxArg);
}

void TTransactedArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .DoIf(TxArg.isSet(), [=] (TFluentMap fluent) {
            TYson txYson = TxArg.getValue();
            ValidateYson(txYson);
            fluent.Item("transaction_id").Node(txYson);
        });

    TArgsParserBase::BuildRequest(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TGetArgsParser::TGetArgsParser()
    : PathArg("path", "path to an object in Cypress that must be retrieved", true, "", "path")
{
    CmdLine.add(PathArg);
}

void TGetArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path);

    TTransactedArgsParser::BuildRequest(consumer);
    BuildOptions(consumer);
}

Stroka TGetArgsParser::GetDriverCommandName() const
{
    return "get";
}

////////////////////////////////////////////////////////////////////////////////

TSetArgsParser::TSetArgsParser()
    : PathArg("path", "path to an object in Cypress that must be set", true, "", "path")
    , ValueArg("value", "value to set", true, "", "yson")
{
    CmdLine.add(PathArg);
    CmdLine.add(ValueArg);
}

void TSetArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path)
        .Item("value").Node(ValueArg.getValue());

    TTransactedArgsParser::BuildRequest(consumer);
    BuildOptions(consumer);
}

Stroka TSetArgsParser::GetDriverCommandName() const
{
    return "set";
}

////////////////////////////////////////////////////////////////////////////////

TRemoveArgsParser::TRemoveArgsParser()
    : PathArg("path", "path to an object in Cypress that must be removed", true, "", "path")
{
    CmdLine.add(PathArg);
}

void TRemoveArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path);

    TTransactedArgsParser::BuildRequest(consumer);
    BuildOptions(consumer);
}

Stroka TRemoveArgsParser::GetDriverCommandName() const
{
    return "remove";
}

////////////////////////////////////////////////////////////////////////////////

TListArgsParser::TListArgsParser()
    : PathArg("path", "path to a object in Cypress whose children must be listed", true, "", "path")
{
    CmdLine.add(PathArg);
}

void TListArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path);
 
    TTransactedArgsParser::BuildRequest(consumer);
    BuildOptions(consumer);
}

Stroka TListArgsParser::GetDriverCommandName() const
{
    return "list";
}

////////////////////////////////////////////////////////////////////////////////

TCreateArgsParser::TCreateArgsParser()
    : TypeArg("type", "type of node", true, NObjectServer::EObjectType::Null, "object type")
    , PathArg("path", "path for a new object in Cypress", true, "", "ypath")
{
    CmdLine.add(TypeArg);
    CmdLine.add(PathArg);
}

void TCreateArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path)
        .Item("type").Scalar(TypeArg.getValue().ToString());

    TTransactedArgsParser::BuildRequest(consumer);
    BuildOptions(consumer);
}

Stroka TCreateArgsParser::GetDriverCommandName() const
{
    return "create";
}

////////////////////////////////////////////////////////////////////////////////

TLockArgsParser::TLockArgsParser()
    : PathArg("path", "path to an object in Cypress that must be locked", true, "", "path")
    , ModeArg("", "mode", "lock mode", false, NCypress::ELockMode::Exclusive, "snapshot, shared, exclusive")
{
    CmdLine.add(PathArg);
    CmdLine.add(ModeArg);
}

void TLockArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path)
        .Item("mode").Scalar(ModeArg.getValue().ToString());

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TLockArgsParser::GetDriverCommandName() const
{
    return "lock";
}

////////////////////////////////////////////////////////////////////////////////

void TStartTxArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    TTransactedArgsParser::BuildRequest(consumer);
    BuildOptions(consumer);
}

Stroka TStartTxArgsParser::GetDriverCommandName() const
{
    return "start_tx";
}

////////////////////////////////////////////////////////////////////////////////

Stroka TRenewTxArgsParser::GetDriverCommandName() const
{
    return "renew_tx";
}

////////////////////////////////////////////////////////////////////////////////

Stroka TCommitTxArgsParser::GetDriverCommandName() const
{
    return "commit_tx";
}

////////////////////////////////////////////////////////////////////////////////

Stroka TAbortTxArgsParser::GetDriverCommandName() const
{
    return "abort_tx";
}

////////////////////////////////////////////////////////////////////////////////

TReadArgsParser::TReadArgsParser()
    : PathArg("path", "path to a table in Cypress that must be read", true, "", "ypath")
{
    CmdLine.add(PathArg);
}

void TReadArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("read")
        .Item("path").Scalar(path);

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TReadArgsParser::GetDriverCommandName() const
{
    return "read";
}

////////////////////////////////////////////////////////////////////////////////

TWriteArgsParser::TWriteArgsParser()
    : PathArg("path", "path to a table in Cypress that must be written", true, "", "ypath")
    , ValueArg("value", "row(s) to write", false, "", "yson")
    , KeyColumnsArg("", "sorted", "key columns names (table must initially be empty, input data must be sorted)", false, "", "list_fragment")
{
    CmdLine.add(PathArg);
    CmdLine.add(ValueArg);
    CmdLine.add(KeyColumnsArg);
}

void TWriteArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());
    auto value = ValueArg.getValue();
    // TODO(babenko): refactor
    auto keyColumns = DeserializeFromYson< yvector<Stroka> >("[" + KeyColumnsArg.getValue() + "]");

    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("write")
        .Item("path").Scalar(path)
        .DoIf(!keyColumns.empty(), [=] (TFluentMap fluent) {
            fluent.Item("sorted").Scalar(true);
            fluent.Item("key_columns").List(keyColumns);
        })
        .DoIf(!value.empty(), [=] (TFluentMap fluent) {
                fluent.Item("value").Node(value);
        });

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TWriteArgsParser::GetDriverCommandName() const
{
    return "write";
}

////////////////////////////////////////////////////////////////////////////////

TUploadArgsParser::TUploadArgsParser()
    : PathArg("path", "to a new file in Cypress that must be uploaded", true, "", "ypath")
{
    CmdLine.add(PathArg);
}

void TUploadArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path);

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TUploadArgsParser::GetDriverCommandName() const
{
    return "upload";
}

////////////////////////////////////////////////////////////////////////////////

TDownloadArgsParser::TDownloadArgsParser()
    : PathArg("path", "path to a file in Cypress that must be downloaded", true, "", "ypath")
{
    CmdLine.add(PathArg);
}

void TDownloadArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto path = PreprocessYPath(PathArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("path").Scalar(path);

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TDownloadArgsParser::GetDriverCommandName() const
{
    return "download";
}

////////////////////////////////////////////////////////////////////////////////

class TStartOpArgsParser::TOperationTracker
{
public:
    TOperationTracker(
        TArgsParserBase::TConfig::TPtr config,
        IDriverPtr driver,
        const TOperationId& operationId)
        : Config(config)
        , Driver(driver)
        , OperationId(operationId)
    { }

    void Run()
    {
        TSchedulerServiceProxy proxy(Driver->GetCommandHost()->GetSchedulerChannel());

        while (true)  {
            auto waitOpReq = proxy.WaitForOperation();
            *waitOpReq->mutable_operation_id() = OperationId.ToProto();
            waitOpReq->set_timeout(Config->OperationWaitTimeout.GetValue());

            // Override default timeout.
            waitOpReq->SetTimeout(Config->OperationWaitTimeout * 2);
            auto waitOpRsp = waitOpReq->Invoke().Get();

            if (!waitOpRsp->IsOK()) {
                ythrow yexception() << waitOpRsp->GetError().ToString();
            }

            if (waitOpRsp->finished())
                break;

            DumpProgress();
        }

        DumpResult();
    }

private:
    TArgsParserBase::TConfig::TPtr Config;
    IDriverPtr Driver;
    TOperationId OperationId;

    // TODO(babenko): refactor
    // TODO(babenko): YPath and RPC responses currently share no base class.
    template <class TResponse>
    static void CheckResponse(TResponse response, const Stroka& failureMessage) 
    {
        if (response->IsOK())
            return;

        ythrow yexception() << failureMessage + "\n" + response->GetError().ToString();
    }

    void DumpProgress()
    {
        auto operationPath = GetOperationPath(OperationId);

        TCypressServiceProxy proxy(Driver->GetCommandHost()->GetMasterChannel());
        auto batchReq = proxy.ExecuteBatch();

        {
            auto req = TYPathProxy::Get(operationPath + "/@state");
            batchReq->AddRequest(req, "get_state");
        }

        {
            auto req = TYPathProxy::Get(operationPath + "/@progress");
            batchReq->AddRequest(req, "get_progress");
        }

        auto batchRsp = batchReq->Invoke().Get();
        CheckResponse(batchRsp, "Error getting operation progress");

        EOperationState state;
        {
            auto rsp = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_state");
            CheckResponse(rsp, "Error getting operation state");
            state = DeserializeFromYson<EOperationState>(rsp->value());
        }

        TYson progress;
        {
            auto rsp = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_progress");
            CheckResponse(rsp, "Error getting operation progress");
            progress = rsp->value();
        }

        if (state == EOperationState::Running) {
            i64 jobsTotal = DeserializeFromYson<i64>(progress, "/jobs/total");
            i64 jobsCompleted = DeserializeFromYson<i64>(progress, "/jobs/completed");
            int donePercentage  = (jobsCompleted * 100) / jobsTotal;
            printf("%s: %3d%% jobs done (%" PRId64 " of %" PRId64 ")\n",
                ~state.ToString(),
                donePercentage,
                jobsCompleted,
                jobsTotal);
        } else {
            printf("%s\n", ~state.ToString());
        }
    }

    void DumpResult()
    {
        auto operationPath = GetOperationPath(OperationId);

        TCypressServiceProxy proxy(Driver->GetCommandHost()->GetMasterChannel());
        auto batchReq = proxy.ExecuteBatch();

        {
            auto req = TYPathProxy::Get(operationPath + "/@result");
            batchReq->AddRequest(req, "get_result");
        }

        auto batchRsp = batchReq->Invoke().Get();
        CheckResponse(batchRsp, "Error getting operation result");

        {
            auto rsp = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_result");
            CheckResponse(rsp, "Error getting operation result");
            // TODO(babenko): refactor!
            auto errorNode = DeserializeFromYson<INodePtr>(rsp->value(), "/error");
            auto error = TError::FromYson(errorNode);
            if (!error.IsOK()) {
                ythrow yexception() << error.ToString();
            }
        }

        printf("Operation completed successfully\n");
    }
};

////////////////////////////////////////////////////////////////////////////////

TStartOpArgsParser::TStartOpArgsParser()
    : NoTrackArg("", "no_track", "don't track operation progress")
{
    CmdLine.add(NoTrackArg);
}

TError TStartOpArgsParser::Execute(const std::vector<std::string>& args)
{
    if (NoTrackArg.getValue()) {
        return TArgsParserBase::Execute(args);
    }

    auto request = ParseArgs(args);
    auto config = ParseConfig();

    printf("Starting %s operation... ", ~GetDriverCommandName().Quote());

    TInterceptingDriverHost driverHost;
    auto driver = CreateDriver(config, &driverHost);
    auto error = driver->Execute(GetDriverCommandName(), request);

    if (!error.IsOK()) {
        printf("failed\n");
        ythrow yexception() << error.ToString();
    }

    auto operationId = DeserializeFromYson<TOperationId>(driverHost.GetOutput());
    printf("done, %s\n", ~operationId.ToString());

    TOperationTracker tracker(config, driver, operationId);
    tracker.Run();

    return TError();
}

////////////////////////////////////////////////////////////////////////////////

TMapArgsParser::TMapArgsParser()
    : InArg("", "in", "input tables", false, "ypath")
    , OutArg("", "out", "output tables", false, "ypath")
    , FilesArg("", "file", "additional files", false, "ypath")
    , MapperArg("", "mapper", "mapper shell command", true, "", "command")
{
    CmdLine.add(InArg);
    CmdLine.add(OutArg);
    CmdLine.add(FilesArg);
    CmdLine.add(MapperArg);
}

void TMapArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto input = PreprocessYPaths(InArg.getValue());
    auto output = PreprocessYPaths(OutArg.getValue());
    auto files = PreprocessYPaths(FilesArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("spec").BeginMap()
            .Item("mapper").Scalar(MapperArg.getValue())
            .Item("input_table_paths").List(input)
            .Item("output_table_paths").List(output)
            .Item("files").List(files)
            .Do(BIND(&TMapArgsParser::BuildOptions, Unretained(this)))
        .EndMap();

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TMapArgsParser::GetDriverCommandName() const
{
    return "map";
}

////////////////////////////////////////////////////////////////////////////////

TMergeArgsParser::TMergeArgsParser()
    : InArg("", "in", "input tables", false, "ypath")
    , OutArg("", "out", "output table", false, "", "ypath")
    , ModeArg("", "mode", "merge mode", false, TMode(EMergeMode::Unordered), "unordered, ordered, sorted")
    , CombineArg("", "combine", "combine small output chunks into larger ones")
{
    CmdLine.add(InArg);
    CmdLine.add(OutArg);
    CmdLine.add(ModeArg);
    CmdLine.add(CombineArg);
}

void TMergeArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto input = PreprocessYPaths(InArg.getValue());
    auto output = PreprocessYPath(OutArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("spec").BeginMap()
            .Item("input_table_paths").List(input)
            .Item("output_table_path").Scalar(output)
            .Item("mode").Scalar(FormatEnum(ModeArg.getValue().Get()))
            .Item("combine_chunks").Scalar(CombineArg.getValue())
            .Do(BIND(&TMergeArgsParser::BuildOptions, Unretained(this)))
        .EndMap();

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TMergeArgsParser::GetDriverCommandName() const
{
    return "merge";
}

////////////////////////////////////////////////////////////////////////////////

TSortArgsParser::TSortArgsParser()
    : InArg("", "in", "input tables", false, "ypath")
    , OutArg("", "out", "output table", false, "", "ypath")
    , KeyColumnsArg("", "key_columns", "key columns names", true, "", "list_fragment")
{
    CmdLine.add(InArg);
    CmdLine.add(OutArg);
    CmdLine.add(KeyColumnsArg);
}

void TSortArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto input = PreprocessYPaths(InArg.getValue());
    auto output = PreprocessYPath(OutArg.getValue());
    // TODO(babenko): refactor
    auto keyColumns = DeserializeFromYson< yvector<Stroka> >("[" + KeyColumnsArg.getValue() + "]");

    BuildYsonMapFluently(consumer)
        .Item("spec").BeginMap()
            .Item("input_table_paths").List(input)
            .Item("output_table_path").Scalar(output)
            .Item("key_columns").List(keyColumns)
        .EndMap();
}

Stroka TSortArgsParser::GetDriverCommandName() const
{
    return "sort";
}

////////////////////////////////////////////////////////////////////////////////

TEraseArgsParser::TEraseArgsParser()
    : InArg("", "in", "input table", false, "", "ypath")
    , OutArg("", "out", "output table", false, "", "ypath")
    , CombineArg("", "combine", "combine small output chunks into larger ones")
{
    CmdLine.add(InArg);
    CmdLine.add(OutArg);
    CmdLine.add(CombineArg);
}

void TEraseArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    auto input = PreprocessYPath(InArg.getValue());
    auto output = PreprocessYPath(OutArg.getValue());

    BuildYsonMapFluently(consumer)
        .Item("spec").BeginMap()
            .Item("input_table_path").Scalar(input)
            .Item("output_table_path").Scalar(output)
            .Item("combine_chunks").Scalar(CombineArg.getValue())
            .Do(BIND(&TEraseArgsParser::BuildOptions, Unretained(this)))
        .EndMap();

    TTransactedArgsParser::BuildRequest(consumer);
}

Stroka TEraseArgsParser::GetDriverCommandName() const
{
    return "erase";
}

////////////////////////////////////////////////////////////////////////////////

TAbortOpArgsParser::TAbortOpArgsParser()
    : OpArg("", "op", "id of an operation that must be aborted", true, "", "operation_id")
{
    CmdLine.add(OpArg);
}

void TAbortOpArgsParser::BuildRequest(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("operation_id").Scalar(OpArg.getValue());

    TArgsParserBase::BuildRequest(consumer);
}

Stroka TAbortOpArgsParser::GetDriverCommandName() const
{
    return "abort_op";
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
