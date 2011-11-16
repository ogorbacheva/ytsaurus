#pragma once

#include "common.h"
#include "ypath_service.h"

#include "../misc/ref.h"
#include "../misc/property.h"
#include "../bus/message.h"
#include "../rpc/client.h"
#include "../rpc/message.h"
#include "../actions/action_util.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TYPathRequest;

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathRequest;

class TYPathResponse;

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathResponse;

////////////////////////////////////////////////////////////////////////////////

class TYPathRequest
    : public TRefCountedBase
{
    DECLARE_BYVAL_RO_PROPERTY(Verb, Stroka);
    DECLARE_BYVAL_RW_PROPERTY(Path, TYPath);
    DECLARE_BYREF_RW_PROPERTY(Attachments, yvector<TSharedRef>);

public:
    typedef TIntrusivePtr<TYPathRequest> TPtr;
    
    TYPathRequest(const Stroka& verb, TYPath path);

    NBus::IMessage::TPtr Serialize();

protected:
    virtual bool SerializeBody(TBlob* data) const = 0;

};

////////////////////////////////////////////////////////////////////////////////

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathRequest
    : public TYPathRequest
    , public TRequestMessage
{
public:
    typedef TTypedYPathResponse<TRequestMessage, TResponseMessage> TTypedResponse;
    typedef TIntrusivePtr< TTypedYPathRequest<TRequestMessage, TResponseMessage> > TPtr;

    TTypedYPathRequest(const Stroka& verb, TYPath path)
        : TYPathRequest(verb, path)
    { }

protected:
    virtual bool SerializeBody(TBlob* data) const
    {
        return NRpc::SerializeMessage(this, data);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TYPathResponse
    : public TRefCountedBase
{
    DECLARE_BYREF_RW_PROPERTY(Attachments, yvector<TSharedRef>);
    DECLARE_BYVAL_RW_PROPERTY(Error, NRpc::TError);

public:
    typedef TIntrusivePtr<TYPathResponse> TPtr;

    void Deserialize(NBus::IMessage* message);

    NRpc::EErrorCode GetErrorCode() const;
    bool IsOK() const;

protected:
    virtual bool DeserializeBody(TRef data) = 0;

};

////////////////////////////////////////////////////////////////////////////////

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathResponse
    : public TYPathResponse
    , public TResponseMessage
{
public:
    typedef TIntrusivePtr< TTypedYPathResponse<TRequestMessage, TResponseMessage> > TPtr;

protected:
    virtual bool DeserializeBody(TRef data)
    {
        return NRpc::DeserializeMessage(this, data);
    }

};

////////////////////////////////////////////////////////////////////////////////

#define YPATH_PROXY_METHOD(ns, method) \
    typedef ::NYT::NYTree::TTypedYPathRequest<ns::TReq##method, ns::TRsp##method> TReq##method; \
    typedef ::NYT::NYTree::TTypedYPathResponse<ns::TReq##method, ns::TRsp##method> TRsp##method; \
    \
    static TReq##method::TPtr method(::NYT::NYTree::TYPath path) \
    { \
        return New<TReq##method>(#method, path); \
    }

////////////////////////////////////////////////////////////////////////////////

//! Executes a YPath verb against a local service.
template <class TTypedRequest>
TIntrusivePtr< TFuture< TIntrusivePtr<typename TTypedRequest::TTypedResponse> > >
ExecuteYPath(IYPathService* rootService, TTypedRequest* request);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT


// TODO: move to ypath_client-inl.h

#include "ypath_detail.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

template <class TTypedRequest, class TTypedResponse>
void OnYPathResponse(
    const TYPathResponseHandlerParam& param,
    TIntrusivePtr< TFuture< TIntrusivePtr<TTypedResponse> > > asyncResponse,
    const Stroka& verb,
    TYPath resolvedPath)
{
    auto response = New<TTypedResponse>();
    response->Deserialize(~param.Message);
    if (!response->IsOK()) {
        auto error = response->GetError();
        Stroka message = Sprintf("Error executing YPath operation (Verb: %s, ResolvedPath: %s)\n%s",
            ~verb,
            ~resolvedPath,
            ~error.GetMessage());
        response->SetError(NRpc::TError(error.GetCode(), message));
    }
    asyncResponse->Set(response);
}

template <class TTypedRequest>
TIntrusivePtr< TFuture< TIntrusivePtr<typename TTypedRequest::TTypedResponse> > >
ExecuteYPath(IYPathService* rootService, TTypedRequest* request)
{
    TYPath path = request->GetPath();
    Stroka verb = request->GetVerb();

    IYPathService::TPtr suffixService;
    TYPath suffixPath;
    ResolveYPath(rootService, path, false, &suffixService, &suffixPath);

    // TODO: can we avoid this?
    request->SetPath(suffixPath);

    auto requestMessage = request->Serialize();
    auto asyncResponse = New< TFuture< TIntrusivePtr<typename TTypedRequest::TTypedResponse> > >();

    auto context = CreateYPathContext(
        ~requestMessage,
        suffixPath,
        verb,
        YTreeLogger.GetCategory(),
        ~FromMethod(
            &OnYPathResponse<TTypedRequest, typename TTypedRequest::TTypedResponse>,
            asyncResponse,
            verb,
            ComputeResolvedYPath(path, suffixPath)));

    try {
        suffixService->Invoke(~context);
    } catch (const NRpc::TServiceException& ex) {
        context->Reply(NRpc::TError(
            EYPathErrorCode(EYPathErrorCode::GenericError),
            ex.what()));
    }

    return asyncResponse;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
