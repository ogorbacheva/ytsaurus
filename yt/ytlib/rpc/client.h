#pragma once

#include "channel.h"

#include "../misc/property.h"
#include "../misc/delayed_invoker.h"
#include "../misc/metric.h"
#include "../misc/serialize.h"
#include "../bus/bus_client.h"
#include "../actions/future.h"

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

class TClientRequest;

template <class TRequestMessage, class TResponseMessage>
class TTypedClientRequest;

class TClientResponse;

template<class TRequestMessage, class TResponseMessage>
class TTypedClientResponse;

////////////////////////////////////////////////////////////////////////////////

class TProxyBase
    : public TNonCopyable
{
protected:
    //! Service error type.
    /*!
     * Defines a basic type of error code for all proxies.
     * A derived proxy type may hide this definition by introducing
     * an appropriate descendant of NRpc::EErrorCode.
     */
    typedef NRpc::EErrorCode EErrorCode;

    TProxyBase(IChannel* channel, const Stroka& serviceName);

    DEFINE_BYVAL_RW_PROPERTY(TDuration, Timeout);

    IChannel::TPtr Channel;
    Stroka ServiceName;
};          

////////////////////////////////////////////////////////////////////////////////

struct IClientRequest
    : virtual public TRefCountedBase
{
    typedef TIntrusivePtr<IClientRequest> TPtr;

    virtual NBus::IMessage::TPtr Serialize() const = 0;

    virtual TRequestId GetRequestId() const = 0;
    virtual Stroka GetPath() const = 0;
    virtual Stroka GetVerb() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TClientRequest
    : public IClientRequest
{
    DEFINE_BYVAL_RO_PROPERTY(Stroka, Path);
    DEFINE_BYVAL_RO_PROPERTY(Stroka, Verb);
    DEFINE_BYREF_RW_PROPERTY(yvector<TSharedRef>, Attachments);
    DEFINE_BYVAL_RO_PROPERTY(TRequestId, RequestId);

public:
    typedef TIntrusivePtr<TClientRequest> TPtr;

    NBus::IMessage::TPtr Serialize() const;

protected:
    IChannel::TPtr Channel;

    TClientRequest(
        IChannel* channel,
        const Stroka& path,
        const Stroka& verb);

    virtual bool SerializeBody(TBlob* data) const = 0;

    void DoInvoke(TClientResponse* response, TDuration timeout);

};

////////////////////////////////////////////////////////////////////////////////

template <class TRequestMessage, class TResponseMessage>
class TTypedClientRequest
    : public TClientRequest
    , public TRequestMessage
{
private:
    typedef TTypedClientResponse<TRequestMessage, TResponseMessage> TTypedResponse;
    TDuration Timeout;

public:
    typedef TTypedClientRequest<TRequestMessage, TResponseMessage> TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    TTypedClientRequest(
        IChannel* channel,
        const Stroka& path,
        const Stroka& verb)
        : TClientRequest(channel, path, verb)
    {
        YASSERT(channel != NULL);
    }

    typename TFuture< TIntrusivePtr<TTypedResponse> >::TPtr Invoke()
    {
        auto response = NYT::New< TTypedClientResponse<TRequestMessage, TResponseMessage> >(GetRequestId());
        auto asyncResult = response->GetAsyncResult();
        DoInvoke(~response, Timeout);
        return asyncResult;
    }

    TIntrusivePtr<TThis> SetTimeout(TDuration timeout)
    {
        Timeout = timeout;
        return this;
    }

private:
    virtual bool SerializeBody(TBlob* data) const
    {
        return SerializeProtobuf(this, data);
    }
};

////////////////////////////////////////////////////////////////////////////////

//! Handles response for an RPC request.
struct IClientResponseHandler
    : virtual TRefCountedBase
{
    typedef TIntrusivePtr<IClientResponseHandler> TPtr;

    //! The delivery of the request has been successfully acknowledged.
    virtual void OnAcknowledgement() = 0;
    //! The request has been replied with #EErrorCode::OK.
    /*!
     *  \param message A message containing the response.
     */
    virtual void OnResponse(NBus::IMessage* message) = 0;
    //! The request has failed.
    /*!
     *  \param error An error that has occurred.
     */
    virtual void OnError(const TError& error) = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TClientResponse
    : public IClientResponseHandler
{
    DEFINE_BYVAL_RO_PROPERTY(TRequestId, RequestId);
    DEFINE_BYREF_RW_PROPERTY(yvector<TSharedRef>, Attachments);
    DEFINE_BYVAL_RO_PROPERTY(NRpc::TError, Error);
    DEFINE_BYVAL_RO_PROPERTY(TInstant, StartTime);

public:
    typedef TIntrusivePtr<TClientResponse> TPtr;

    NBus::IMessage::TPtr GetResponseMessage() const;

    int GetErrorCode() const;
    bool IsOK() const;

protected:
    TClientResponse(const TRequestId& requestId);

    virtual bool DeserializeBody(TRef data) = 0;
    virtual void FireCompleted() = 0;

private:
    friend class TClientRequest;

    DECLARE_ENUM(EState,
        (Sent)
        (Ack)
        (Done)
    );

    // Protects state.
    TSpinLock SpinLock;
    EState State;
    NBus::IMessage::TPtr ResponseMessage;

    // IClientResponseHandler implementation.
    virtual void OnAcknowledgement();
    virtual void OnResponse(NBus::IMessage* message);
    virtual void OnError(const TError& error);

    void Deserialize(NBus::IMessage* responseMessage);

};

////////////////////////////////////////////////////////////////////////////////

template <class TRequestMessage, class TResponseMessage>
class TTypedClientResponse
    : public TClientResponse
    , public TResponseMessage
{
public:
    typedef TIntrusivePtr<TTypedClientResponse> TPtr;

    TTypedClientResponse(const TRequestId& requestId)
        : TClientResponse(requestId)
        , AsyncResult(NYT::New< TFuture<TPtr> >())
    { }

private:
    friend class TTypedClientRequest<TRequestMessage, TResponseMessage>;

    typename TFuture<TPtr>::TPtr AsyncResult;

    typename TFuture<TPtr>::TPtr GetAsyncResult()
    {
        return AsyncResult;
    }

    virtual void FireCompleted()
    {
        AsyncResult->Set(this);
        AsyncResult.Reset();
    }

    virtual bool DeserializeBody(TRef data)
    {
        return DeserializeProtobuf(this, data);
    }
};

////////////////////////////////////////////////////////////////////////////////

#define RPC_DECLARE_PROXY(path, errorCodes) \
    static Stroka GetServiceName() \
    { \
        return PP_STRINGIZE(path); \
    } \
    \
    DECLARE_ENUM(E##path##Error, \
        errorCodes \
    ); \
    \
    typedef E##path##Error EErrorCode;

////////////////////////////////////////////////////////////////////////////////

#define DEFINE_RPC_PROXY_METHOD(ns, method) \
    typedef ::NYT::NRpc::TTypedClientRequest<ns::TReq##method, ns::TRsp##method> TReq##method; \
    typedef ::NYT::NRpc::TTypedClientResponse<ns::TReq##method, ns::TRsp##method> TRsp##method; \
    typedef ::NYT::TFuture<TRsp##method::TPtr> TInv##method; \
    \
    TReq##method::TPtr method() \
    { \
        return \
            New<TReq##method>(~Channel, ServiceName, #method) \
            ->SetTimeout(Timeout_); \
    }

////////////////////////////////////////////////////////////////////////////////

// TODO: deprecate
#define USE_RPC_PROXY_METHOD(TProxy, method) \
    typedef TProxy::TReq##method TReq##method; \
    typedef TProxy::TRsp##method TRsp##method; \
    typedef TProxy::TInv##method TInv##method;

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
