#include "driver.h"

#include <ytlib/ytree/ytree.h>
#include <ytlib/driver/config.h>
#include <ytlib/driver/format.h>

#include <string>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

COMMON_V8_USES

using v8::Context;
using v8::TryCatch;

using namespace NYTree;
using namespace NDriver;

////////////////////////////////////////////////////////////////////////////////

// XXX(sandello): This is temporary; awaiting merge of 
// "babenko/new_driver" into mainline.

//! An instance of driver request.
struct TDriverRequest
{
    //! Command name to execute.
    std::string CommandName;

    //! Stream used for reading command input.
    TInputStream* InputStream;

    //! Format used for reading the input.
    TFormat InputFormat;

    //! Stream where the command output is written.
    TOutputStream* OutputStream;

    //! Format used for writing the output.
    TFormat OutputFormat;

    //! A map containing command arguments.
    NYTree::IMapNodePtr Parameters;
};

//! An instance of driver request.
struct TDriverResponse
{
    //! An error returned by the command, if any.
    int Error;
};

struct IDriver
{
    TDriverResponse Execute(const TDriverRequest& request);
};

IDriverPtr CreateDriver(TDriverConfigPtr config);

////////////////////////////////////////////////////////////////////////////////

namespace {
struct TExecuteRequest
{
    uv_work_t Request;
    TNodeJSDriver* Host;
    TNodeJSInputStream* InputStream;
    TNodeJSOutputStream* OutputStream;

    Persistent<Function> Callback;

    TDriverRequest DriverRequest;
    TDriverResponse DriverResponse;

    TExecuteRequest(TNodeJSDriver* host, Handle<Function> callback)
        : Host(host)
        , Callback(Persistent<Function>::New(callback))
    {
        THREAD_AFFINITY_IS_V8();

        Host->Ref();
        // TODO(sandello): Ref streams here also.
    }

    ~TExecuteRequest()
    {
        THREAD_AFFINITY_IS_V8();

        Callback.Dispose();
        Callback.Clear();

        // TODO(sandello): Unref streams here also.
        Host->Unref();
    }
};
} // namespace

Persistent<FunctionTemplate> TNodeJSDriver::ConstructorTemplate;

////////////////////////////////////////////////////////////////////////////////

TNodeJSDriver::TNodeJSDriver(const NYTree::TYson& configuration)
    : node::ObjectWrap()
{
    T_THREAD_AFFINITY_IS_V8();

    INodePtr configNode;
    try {
        configNode = DeserializeFromYson(configuration);
    } catch (const std::exception& ex) {
        Driver = NULL;
        Message = Sprintf("Error reading configuration\n%s", ex.what());
    }

    auto config = New<TDriverConfig>();
    try {
        config->Load(~configNode);
    } catch (const std::exception& ex) {
        Driver = NULL;
        Message = Sprintf("Error parsing configuration\n%s", ex.what());
    }

    Driver = CreateDriver(config);
}

TNodeJSDriver::~TNodeJSDriver()
{
    T_THREAD_AFFINITY_IS_V8();
}

////////////////////////////////////////////////////////////////////////////////

void TNodeJSDriver::Initialize(Handle<Object> target)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    ConstructorTemplate = Persistent<FunctionTemplate>::New(
        FunctionTemplate::New(TNodeJSDriver::New));

    ConstructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);
    ConstructorTemplate->SetClassName(String::NewSymbol("TNodeJSDriver"));

    NODE_SET_PROTOTYPE_METHOD(ConstructorTemplate, "Execute", TNodeJSDriver::Execute);

    target->Set(
        String::NewSymbol("TNodeJSDriver"),
        ConstructorTemplate->GetFunction());
}

bool TNodeJSDriver::HasInstance(Handle<Value> value)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    return
        value->IsObject() &&
        ConstructorTemplate->HasInstance(value->ToObject());
}

Handle<Value> TNodeJSDriver::New(const Arguments& args)
{
    T_THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    YASSERT(args.Length() == 1);
    EXPECT_THAT_IS(args[0], String);

    String::AsciiValue configuration(args[0]);

    TNodeJSDriver* host = new TNodeJSDriver(
        Stroka(*configuration, configuration.length()));
    host->Wrap(args.This());

    if (host->Driver) {
        return args.This();
    } else {
        return ThrowException(Exception::Error(String::New(~host->Message)));
    }
}

////////////////////////////////////////////////////////////////////////////////

Handle<Value> TNodeJSDriver::Execute(const Arguments& args)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    // Validate arguments.
    // These arguments are used to fill TDriverRequest structure,
    // hence we have to validate all the arguments as early as possible.
    YASSERT(args.Length() == 7);

    EXPECT_THAT_IS(args[0], String); // CommandName
    EXPECT_THAT_HAS_INSTANCE(args[1], TNodeJSInputStream); // InputStream
    EXPECT_THAT_IS(args[2], String); // InputFormat
    EXPECT_THAT_HAS_INSTANCE(args[3], TNodeJSOutputStream); // OutputStream
    EXPECT_THAT_IS(args[4], String); // OutputFormat
    EXPECT_THAT_IS(args[5], Object); // Parameters
    EXPECT_THAT_IS(args[6], Function); // Callback

    // Unwrap arguments.
    String::AsciiValue commandName(args[0]);
    TNodeJSInputStream* inputStream =
        ObjectWrap::Unwrap<TNodeJSInputStream>(args[1].As<Object>());
    INodePtr inputFormat =
        ConvertV8StringToYson(args[2].As<String>());
    TNodeJSOutputStream* outputStream =
        ObjectWrap::Unwrap<TNodeJSOutputStream>(args[3].As<Object>());
    INodePtr outputFormat =
        ConvertV8StringToYson(args[4].As<String>());
    INodePtr parameters =
        ConvertV8ValueToYson(args[5].As<Object>());
    Local<Function> callback = args[6].As<Function>();

    // Build an atom of work.
    YASSERT(parameters->GetType() == ENodeType::Map);

    TExecuteRequest* request = new TExecuteRequest(
        ObjectWrap::Unwrap<TNodeJSDriver>(args.This()),
        callback);

    // Fill in TDriverRequest structure.
    request->DriverRequest.CommandName = std::string(*commandName, commandName.length());
    request->DriverRequest.InputStream = inputStream;
    request->DriverRequest.InputFormat = TFormat::FromYson(inputFormat);
    request->DriverRequest.OutputStream = outputStream;
    request->DriverRequest.OutputFormat = TFormat::FromYson(outputFormat);
    request->DriverRequest.Parameters = parameters->AsMap();

    fprintf(stderr, "WOOHOO! We are executing %s [%p->%p]!\n",
        *commandName,
        inputStream,
        outputStream);

    uv_queue_work(
        uv_default_loop(), &request->Request,
        TNodeJSDriver::ExecuteWork, TNodeJSDriver::ExecuteAfter);

    return Undefined();
}

void TNodeJSDriver::ExecuteWork(uv_work_t* workRequest)
{
    THREAD_AFFINITY_IS_UV();
    TExecuteRequest* request =
        container_of(workRequest, TExecuteRequest, Request);

    request->DriverResponse = request->Host->Driver->Execute(request->DriverRequest);
}

void TNodeJSDriver::ExecuteAfter(uv_work_t* workRequest)
{
    THREAD_AFFINITY_IS_V8();
    HandleScope scope;

    TExecuteRequest* request =
        container_of(workRequest, TExecuteRequest, Request);

    {
        TryCatch block;

        Local<Value> args[] = {
            // TODO(sandello): Fix me.
            Integer::New(request->DriverResponse.Error)
        };

        request->Callback->Call(
            Context::GetCurrent()->Global(),
            ARRAY_SIZE(args),
            args);

        if (block.HasCaught()) {
            node::FatalException(block);
        }
    }

    delete request;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
