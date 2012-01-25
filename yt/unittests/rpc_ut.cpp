#include "stdafx.h"

#include "rpc_ut.pb.h"

#include <ytlib/misc/error.h>

#include <ytlib/actions/action_queue.h>

#include <ytlib/bus/bus.h>
#include <ytlib/bus/nl_client.h>
#include <ytlib/bus/nl_server.h>

#include <ytlib/rpc/client.h>
#include <ytlib/rpc/server.h>
#include <ytlib/rpc/service.h>

#include <contrib/testing/framework.h>

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

class TMyProxy
    : public TProxyBase
{
public:
    static const Stroka ServiceName;

    TMyProxy(IChannel* channel)
        : TProxyBase(channel, ServiceName)
    { }

    DEFINE_RPC_PROXY_METHOD(NMyRpc, SomeCall);
    DEFINE_RPC_PROXY_METHOD(NMyRpc, ModifyAttachments);
    DEFINE_RPC_PROXY_METHOD(NMyRpc, ReplyingCall);
    DEFINE_RPC_PROXY_METHOD(NMyRpc, EmptyCall);
    DEFINE_RPC_PROXY_METHOD(NMyRpc, CustomMessageError);
    DEFINE_RPC_PROXY_METHOD(NMyRpc, NotRegistredCall);

    DEFINE_ONE_WAY_RPC_PROXY_METHOD(NMyRpc, OneWay);
    DEFINE_ONE_WAY_RPC_PROXY_METHOD(NMyRpc, CheckAll);
    DEFINE_ONE_WAY_RPC_PROXY_METHOD(NMyRpc, NotRegistredOneWay);

};

const Stroka TMyProxy::ServiceName = "MyService";

////////////////////////////////////////////////////////////////////////////////

class TNonExistingServiceProxy
    : public TProxyBase
{
public:
    typedef TIntrusivePtr<TNonExistingServiceProxy> TPtr;

    static const Stroka ServiceName;

    TNonExistingServiceProxy(IChannel* channel)
        : TProxyBase(channel, ServiceName)
    { }

    DEFINE_RPC_PROXY_METHOD(NMyRpc, EmptyCall);
    DEFINE_ONE_WAY_RPC_PROXY_METHOD(NMyRpc, OneWay);
};

const Stroka TNonExistingServiceProxy::ServiceName = "NonExistingService";

////////////////////////////////////////////////////////////////////////////////

Stroka StringFromSharedRef(const TSharedRef& sharedRef)
{
    auto blob = sharedRef.ToBlob();
    return Stroka(blob.begin(), blob.end());
}


TSharedRef SharedRefFromString(const Stroka& s)
{
    TBlob blob(s.begin(), s.end());
    return MoveRV(blob);
}

////////////////////////////////////////////////////////////////////////////////

class TMyService
    : public TServiceBase
{
public:
    typedef TIntrusivePtr<TMyService> TPtr;
    typedef TMyService TThis;
    TMyService(IInvoker* invoker, Event* event)
        : TServiceBase(invoker, TMyProxy::ServiceName, "Main")
        , Event_(event)
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(SomeCall));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ModifyAttachments));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ReplyingCall));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(EmptyCall));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(CustomMessageError));

        RegisterMethod(ONE_WAY_RPC_SERVICE_METHOD_DESC(OneWay));
        RegisterMethod(ONE_WAY_RPC_SERVICE_METHOD_DESC(CheckAll));

        // Note: NotRegistredCall and NotRegistredOneWay are not registred
    }

    DECLARE_RPC_SERVICE_METHOD(NMyRpc, SomeCall);
    DECLARE_RPC_SERVICE_METHOD(NMyRpc, ModifyAttachments);
    DECLARE_RPC_SERVICE_METHOD(NMyRpc, ReplyingCall);
    DECLARE_RPC_SERVICE_METHOD(NMyRpc, EmptyCall);
    DECLARE_RPC_SERVICE_METHOD(NMyRpc, CustomMessageError);

    DECLARE_ONE_WAY_RPC_SERVICE_METHOD(NMyRpc, OneWay);
    DECLARE_ONE_WAY_RPC_SERVICE_METHOD(NMyRpc, CheckAll);

    DECLARE_RPC_SERVICE_METHOD(NMyRpc, NotRegistredCall);
    DECLARE_ONE_WAY_RPC_SERVICE_METHOD(NMyRpc, NotRegistredOneWay);
private:
    // To signal for one-way rpc requests when processed the request
    Event* Event_;
};

DEFINE_RPC_SERVICE_METHOD(TMyService, SomeCall)
{
    int a = request->a();
    response->set_b(a + 100);
    context->Reply();
}

DEFINE_RPC_SERVICE_METHOD(TMyService, ReplyingCall)
{
    UNUSED(request);
    UNUSED(response);
    context->Reply();
}

DEFINE_RPC_SERVICE_METHOD(TMyService, ModifyAttachments)
{
    for (int i = 0; i < request->Attachments().ysize(); ++i) {
        auto blob = request->Attachments()[i].ToBlob();
        blob.push_back('_');

        response->Attachments().push_back(MoveRV(blob));
    }
    context->Reply();
}

DEFINE_RPC_SERVICE_METHOD(TMyService, EmptyCall)
{
    UNUSED(request);
    UNUSED(response);
    UNUSED(context);
}

DEFINE_RPC_SERVICE_METHOD(TMyService, NotRegistredCall)
{
    UNUSED(request);
    UNUSED(response);
    UNUSED(context);
}

DEFINE_RPC_SERVICE_METHOD(TMyService, CustomMessageError)
{

    UNUSED(request);
    UNUSED(response);
    context->Reply(TError(42, "Some Error"));
}

DEFINE_ONE_WAY_RPC_SERVICE_METHOD(TMyService, OneWay)
{
    UNUSED(request);
}

DEFINE_ONE_WAY_RPC_SERVICE_METHOD(TMyService, CheckAll)
{
    EXPECT_EQ(12345, request->value());
    EXPECT_EQ(true, request->ok());
    EXPECT_EQ(Stroka("hello, TMyService"), request->message());

    const auto& attachments = request->Attachments();
    EXPECT_EQ(3, attachments.ysize());
    EXPECT_EQ("Attachments",     StringFromSharedRef(attachments[0]));
    EXPECT_EQ("are",      StringFromSharedRef(attachments[1]));
    EXPECT_EQ("ok",  StringFromSharedRef(attachments[2]));

    Event_->Signal();
}

////////////////////////////////////////////////////////////////////////////////

class TRpcTest
    : public ::testing::Test
{
    IRpcServer::TPtr RpcServer;

    // need to remember
    TActionQueue::TPtr Queue;

public:
    virtual void SetUp()
    {
        auto busConfig = New<NBus::TNLBusServerConfig>();
        busConfig->Port = 2000;
        auto busServer = NBus::CreateNLBusServer(~busConfig);

        RpcServer = CreateRpcServer(~busServer);

        Queue = New<TActionQueue>();

        RpcServer->RegisterService(~New<TMyService>(~Queue->GetInvoker(), &ReadyEvent));
        RpcServer->Start();
    }

    virtual void TearDown()
    {
        RpcServer->Stop();
    }

    // For services to signal when they processed incoming onewey rpc request
    Event ReadyEvent;
};

////////////////////////////////////////////////////////////////////////////////

class TResponseHandler
    : public TRefCounted
{
public:
    typedef TIntrusivePtr<TResponseHandler> TPtr;

    TResponseHandler(int numRepliesWaiting)
        : NumRepliesWaiting(numRepliesWaiting)
    { }

    Event Event_;

    void CheckReply(TMyProxy::TRspSomeCall::TPtr response, int expected)
    {
        EXPECT_IS_TRUE(response->IsOK());
        EXPECT_EQ(expected, response->b());

        --NumRepliesWaiting;
        if (NumRepliesWaiting == 0) {
            Event_.Signal();
        }
    }

private:
    int NumRepliesWaiting;

};

////////////////////////////////////////////////////////////////////////////////

TEST_F(TRpcTest, Send)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->SomeCall();
    request->set_a(42);
    auto response = request->Invoke()->Get();

    EXPECT_IS_TRUE(response->IsOK());
    EXPECT_EQ(142, response->b());
}

TEST_F(TRpcTest, ManyAsyncSends)
{
    int numSends = 1000;
    auto handler = New<TResponseHandler>(numSends);

    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));

    for (int i = 0; i < numSends; ++i) {
        auto request = proxy->SomeCall();
        request->set_a(i);
        request->Invoke()->Subscribe(FromMethod(&TResponseHandler::CheckReply, handler, i + 100));
    }

    EXPECT_IS_TRUE(handler->Event_.WaitT(TDuration::Seconds(4))); // assert no timeout
}

TEST_F(TRpcTest, Attachments)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->ModifyAttachments();

    request->Attachments().push_back(SharedRefFromString("Hello"));
    request->Attachments().push_back(SharedRefFromString("from"));
    request->Attachments().push_back(SharedRefFromString("TMyProxy"));

    auto response = request->Invoke()->Get();

    const auto& attachments = response->Attachments();
    EXPECT_EQ(3, attachments.ysize());
    EXPECT_EQ("Hello_",     StringFromSharedRef(attachments[0]));
    EXPECT_EQ("from_",      StringFromSharedRef(attachments[1]));
    EXPECT_EQ("TMyProxy_",  StringFromSharedRef(attachments[2]));
}

// Now test different types of errors
TEST_F(TRpcTest, OK)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->ReplyingCall();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(TError::OK, response->GetErrorCode());
}

TEST_F(TRpcTest, TransportError)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:9999"));
    auto request = proxy->EmptyCall();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(EErrorCode::TransportError, response->GetErrorCode());
}

TEST_F(TRpcTest, NoService)
{
    TAutoPtr<TNonExistingServiceProxy> proxy = new TNonExistingServiceProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->EmptyCall();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(EErrorCode::NoSuchService, response->GetErrorCode());
}

TEST_F(TRpcTest, NoMethod)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->NotRegistredCall();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(EErrorCode::NoSuchVerb, response->GetErrorCode());
}

TEST_F(TRpcTest, Timeout)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    proxy->SetTimeout(TDuration::Seconds(1));

    auto request = proxy->EmptyCall();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(EErrorCode::Timeout, response->GetErrorCode());
}

TEST_F(TRpcTest, CustomErrorMessage)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->CustomMessageError();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(42, response->GetErrorCode());
    EXPECT_EQ("Some Error", response->GetError().GetMessage());
}

////////////////////////////////////////////////////////////////////////////////

TEST_F(TRpcTest, OneWaySend)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->CheckAll();

    request->set_value(12345);
    request->set_ok(true);
    request->set_message(Stroka("hello, TMyService"));
    request->Attachments().push_back(SharedRefFromString("Attachments"));
    request->Attachments().push_back(SharedRefFromString("are"));
    request->Attachments().push_back(SharedRefFromString("ok"));

    auto response = request->Invoke()->Get();
    EXPECT_EQ(TError::OK, response->GetErrorCode());

    EXPECT_IS_TRUE(ReadyEvent.WaitT(TDuration::Seconds(4))); // assert no timeout
}

// Different types of errors in one-way rpc
// TODO: think about refactoring
TEST_F(TRpcTest, OneWayOK)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->OneWay();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(TError::OK, response->GetErrorCode());
}

TEST_F(TRpcTest, OneWayTransportError)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:9999"));
    auto request = proxy->OneWay();
    auto response = request->Invoke()->Get();

    EXPECT_EQ(EErrorCode::TransportError, response->GetErrorCode());
}

TEST_F(TRpcTest, OneWayNoService)
{
    TAutoPtr<TNonExistingServiceProxy> proxy = new TNonExistingServiceProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->OneWay();
    auto response = request->Invoke()->Get();

    // In this case we receive OK instead of NoSuchService
    EXPECT_EQ(TError::OK, response->GetErrorCode());
}

TEST_F(TRpcTest, OneWayNoMethod)
{
    TAutoPtr<TMyProxy> proxy = new TMyProxy(~CreateBusChannel("localhost:2000"));
    auto request = proxy->NotRegistredOneWay();
    auto response = request->Invoke()->Get();

    // In this case we receive OK instead of NoSuchVerb
    EXPECT_EQ(TError::OK, response->GetErrorCode());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
