#pragma once

#include "common.h"
#include "client.h"
#include "message.h"

#include "../misc/hash.h"
#include "../misc/metric.h"
#include "../logging/log.h"

#include <util/generic/yexception.h>

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

//! Represents an error occured while serving an RPC request.
class TServiceException 
    : public yexception
{
public:
    //! Initializes a new instance.
    explicit TServiceException(EErrorCode errorCode = EErrorCode::ServiceError)
        : ErrorCode(errorCode)
    { }

    //! Gets the error code.
    TError GetError() const
    {
        return TError(ErrorCode, what());
    }

protected:
    EErrorCode ErrorCode;

};

////////////////////////////////////////////////////////////////////////////////

//! A typed version of TServiceException.
/*!
 *  The primary difference from the untyped TServiceException is that the
 *  constructor accepts an error of a given TErrorCode type.
 *  
 *  This enables to capture the error message during exception construction
 *  and write
 *  \code
 *  typedef TTypedServiceException<EMyCode> TMyException;
 *  ythrow TMyException(EMyCode::SomethingWrong);
 *  \endcode
 *  instead of
 *  \code
 *  ythrow TServiceException(EMyCode(EMyCode::SomethingWrong));
 *  \endcode
 */
template <class TErrorCode>
class TTypedServiceException 
    : public TServiceException
{
public:
    //! Initializes a new instance.
    explicit TTypedServiceException(TErrorCode errorCode = EErrorCode::ServiceError)
        : TServiceException(errorCode)
    { }
};

////////////////////////////////////////////////////////////////////////////////

class TServiceContext;

struct IService
    : virtual TRefCountedBase
{
    typedef TIntrusivePtr<IService> TPtr;

    virtual Stroka GetServiceName() const = 0;
    virtual Stroka GetLoggingCategory() const = 0;

    virtual void OnBeginRequest(TIntrusivePtr<TServiceContext> context) = 0;
    virtual void OnEndRequest(TIntrusivePtr<TServiceContext> context) = 0;

    virtual Stroka GetDebugInfo() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TServiceContext
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TServiceContext> TPtr;

    TServiceContext(
        IService::TPtr service,
        TRequestId requestId,
        const Stroka& methodName,
        NBus::IMessage::TPtr message,
        NBus::IBus::TPtr replyBus);

    void Reply(EErrorCode errorCode);
    void Reply(const TError& error);
    bool IsReplied() const;

    TSharedRef GetRequestBody() const;
    void SetResponseBody(TBlob* responseBody);

    const yvector<TSharedRef>& GetRequestAttachments() const;
    void SetResponseAttachments(yvector<TSharedRef>* attachments);

    Stroka GetServiceName() const;
    Stroka GetMethodName() const;
    const TRequestId& GetRequestId() const;

    NBus::IBus::TPtr GetReplyBus() const;

    void SetRequestInfo(const Stroka& info);
    Stroka GetRequestInfo() const;

    void SetResponseInfo(const Stroka& info);
    Stroka GetResponseInfo();

    IAction::TPtr Wrap(IAction::TPtr action);

protected:
    DECLARE_ENUM(EState,
        (Received)
        (Replied)
    );

    EState State;
    IService::TPtr Service;
    TRequestId RequestId;
    Stroka MethodName;
    NBus::IBus::TPtr ReplyBus;
    TSharedRef RequestBody;
    yvector<TSharedRef> RequestAttachments;
    NLog::TLogger ServiceLogger;
    bool Replied;

    TBlob ResponseBody;
    yvector<TSharedRef> ResponseAttachments;

    Stroka RequestInfo;
    Stroka ResponseInfo;

private:
    void DoReply(const TError& error);
    void WrapThunk(IAction::TPtr action) throw();

    void LogException(NLog::ELogLevel level, const TError& error);
    void LogRequestInfo();
    void LogResponseInfo(const TError& error);

    static void AppendInfo(Stroka& lhs, const Stroka& rhs);
};

////////////////////////////////////////////////////////////////////////////////

template<class TRequestMessage, class TResponseMessage>
class TTypedServiceRequest
    : public TRequestMessage
    , private TNonCopyable
{
public:
    TTypedServiceRequest(const yvector<TSharedRef>& attachments)
        : Attachments_(attachments)
    { }

    yvector<TSharedRef>& Attachments()
    {
        return Attachments_;
    }

private:
    yvector<TSharedRef> Attachments_;

};

////////////////////////////////////////////////////////////////////////////////

template<class TRequestMessage, class TResponseMessage>
class TTypedServiceResponse
    : public TResponseMessage
    , private TNonCopyable
{
public:
    yvector<TSharedRef>& Attachments()
    {
        return Attachments_;
    }

private:
    yvector<TSharedRef> Attachments_;

};

////////////////////////////////////////////////////////////////////////////////

// TODO: move impl to inl?
template<class TRequestMesssage, class TResponseMessage>
class TTypedServiceContext
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr< TTypedServiceContext<TRequestMesssage, TResponseMessage> > TPtr;
    typedef TTypedServiceRequest<TRequestMesssage, TResponseMessage> TTypedRequest;
    typedef TTypedServiceResponse<TRequestMesssage, TResponseMessage> TTypedResponse;

    TTypedServiceContext(TServiceContext::TPtr context)
        : Logger(RpcLogger)
        , Context(context)
        , Request_(context->GetRequestAttachments())
    {
        YASSERT(~context != NULL);

        if (!DeserializeMessage(&Request_, context->GetRequestBody())) {
            ythrow TServiceException(EErrorCode::ProtocolError) <<
                "Error deserializing request body";
        }
    }

    TTypedRequest& Request()
    {
        return Request_;
    }

    TTypedResponse& Response()
    {
        return Response_;
    }

    // NB: This overload is added to workaround VS2010 ICE inside lambdas calling Reply.
    void Reply()
    {
        Reply(EErrorCode::OK);
    }

    void Reply(EErrorCode errorCode)
    {
        Reply(TError(errorCode));
    }

    void Reply(const TError& error)
    {
        if (error.IsOK()) {
            TBlob responseData;
            if (!SerializeMessage(&Response_, &responseData)) {
                ythrow TServiceException(EErrorCode::ProtocolError) <<
                    "Error serializing response";
            }
            Context->SetResponseBody(&responseData);
            Context->SetResponseAttachments(&Response_.Attachments());
        }
        Context->Reply(error);
    }

    bool IsReplied() const
    {
        return Context->IsReplied();
    }

    IAction::TPtr Wrap(typename IParamAction<TPtr>::TPtr paramAction)
    {
        YASSERT(~paramAction != NULL);
        return Context->Wrap(paramAction->Bind(TPtr(this)));
    }
    
    void SetRequestInfo(const Stroka& info)
    {
        Context->SetRequestInfo(info);
    }

    void SetRequestInfo(const char* format, ...)
    {
        Stroka info;
        va_list params;
        va_start(params, format);
        vsprintf(info, format, params);
        va_end(params);
        Context->SetRequestInfo(info);
    }

    Stroka GetRequestInfo() const
    {
        return Context->GetRequestInfo();
    }

    void SetResponseInfo(const Stroka& info)
    {
        Context->SetResponseInfo(info);
    }

    void SetResponseInfo(const char* format, ...)
    {
        Stroka info;
        va_list params;
        va_start(params, format);
        vsprintf(info, format, params);
        va_end(params);
        Context->SetResponseInfo(info);
    }

    Stroka GetResponseInfo()
    {
        return Context->GetResponseInfo();
    }

    TServiceContext::TPtr GetUntypedContext() const
    {
        return Context;
    }

private:
    NLog::TLogger& Logger;
    TServiceContext::TPtr Context;
    TTypedRequest Request_;
    TTypedResponse Response_;

};

////////////////////////////////////////////////////////////////////////////////

//! Provides a base for implementing IService.
class TServiceBase
    : public IService
{
public:
    //! Reports debug info of the running service instance.
    Stroka GetDebugInfo() const;

protected:
    //! Describes a handler for a service method.
    typedef IParamAction<TIntrusivePtr<TServiceContext> > THandler;

    //! Information needed to a register a service method.
    struct TMethodDescriptor
    {
        //! Initializes the instance.
        TMethodDescriptor(const Stroka& methodName, THandler::TPtr handler)
            : MethodName(methodName)
            , Handler(handler)
        {
            YASSERT(~handler != NULL);
        }

        //! Service method name.
        Stroka MethodName;
        //! A handler that will serve the requests.
        THandler::TPtr Handler;
    };

    //! Initializes the instance.
    /*!
     *  \param defaultServiceInvoker
     *  An invoker that will be used for serving method invocations unless
     *  configured otherwise (see #RegisterMethod).
     *  
     *  \param serviceName
     *  A name of the service.
     *  
     *  \param loggingCategory
     *  A category that will be used to log various debugging information
     *  regarding service activity.
     */
    TServiceBase(
        IInvoker::TPtr defaultServiceInvoker,
        const Stroka& serviceName,
        const Stroka& loggingCategory);

    //! Registers a method.
    void RegisterMethod(const TMethodDescriptor& descriptor);

    //! Registers a method with a supplied custom invoker.
    void RegisterMethod(const TMethodDescriptor& descriptor, IInvoker::TPtr invoker);

private:
    struct TRuntimeMethodInfo
    {
        TMethodDescriptor Descriptor;
        IInvoker::TPtr Invoker;
        TMetric ExecutionTime;

        TRuntimeMethodInfo(const TMethodDescriptor& info, IInvoker::TPtr invoker)
            : Descriptor(info)
            , Invoker(invoker)
            // TODO: configure properly
            , ExecutionTime(0, 1000, 10)
        { }
    };

    struct TActiveRequest
    {
        TActiveRequest(TRuntimeMethodInfo* runtimeInfo, const TInstant& startTime)
            : RuntimeInfo(runtimeInfo)
            , StartTime(startTime)
        {
            YASSERT(runtimeInfo != NULL);
        }

        TRuntimeMethodInfo* RuntimeInfo;
        TInstant StartTime;
    };
    
    IInvoker::TPtr DefaultServiceInvoker;
    Stroka ServiceName;
    NLog::TLogger ServiceLogger;

    //! Protects #RuntimeMethodInfos and #OutstandingRequests.
    TSpinLock SpinLock;
    yhash_map<Stroka, TRuntimeMethodInfo> RuntimeMethodInfos;
    yhash_map<TServiceContext::TPtr, TActiveRequest> ActiveRequests;

    virtual void OnBeginRequest(TServiceContext::TPtr context);
    virtual void OnEndRequest(TServiceContext::TPtr context);

    virtual Stroka GetLoggingCategory() const;
    virtual Stroka GetServiceName() const;

};

////////////////////////////////////////////////////////////////////////////////

#define RPC_SERVICE_METHOD_DECL(ns, method) \
    typedef ::NYT::NRpc::TTypedServiceRequest<ns::TReq##method, ns::TRsp##method> TReq##method; \
    typedef ::NYT::NRpc::TTypedServiceResponse<ns::TReq##method, ns::TRsp##method> TRsp##method; \
    typedef ::NYT::NRpc::TTypedServiceContext<ns::TReq##method, ns::TRsp##method> TCtx##method; \
    \
    void method##Thunk(::NYT::NRpc::TServiceContext::TPtr context); \
    \
    void method( \
        TCtx##method::TTypedRequest* request, \
        TCtx##method::TTypedResponse* response, \
        TCtx##method::TPtr context)

#define RPC_SERVICE_METHOD_IMPL(type, method) \
    void type::method##Thunk(::NYT::NRpc::TServiceContext::TPtr context) \
    { \
        auto typedContext = New<TCtx##method>(context); \
        method( \
            &typedContext->Request(), \
            &typedContext->Response(), \
            typedContext); \
    } \
    \
    void type::method( \
        TCtx##method::TTypedRequest* request, \
        TCtx##method::TTypedResponse* response, \
        TCtx##method::TPtr context)

#define RPC_SERVICE_METHOD_DESC(method) \
    TMethodDescriptor(#method, FromMethod(&TThis::method##Thunk, this)) \

// TODO: not used, consider dropping
#define USE_RPC_SERVICE_METHOD_LOGGER() \
    ::NYT::NLog::TPrefixLogger Logger( \
        ServiceLogger, \
        context->GetMethodName() + ": ")
        
////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
