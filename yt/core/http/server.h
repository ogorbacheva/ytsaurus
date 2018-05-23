#pragma once

#include "public.h"
#include "http.h"

#include <yt/core/net/address.h>

#include <yt/core/concurrency/public.h>

#include <yt/core/actions/future.h>

#include <yt/core/misc/ref.h>

namespace NYT {
namespace NHttp {

////////////////////////////////////////////////////////////////////////////////

class TCallbackHandler
    : public IHttpHandler
{
public:
    explicit TCallbackHandler(TCallback<void(const IRequestPtr&, const IResponseWriterPtr&)> handler);

    virtual void HandleRequest(const IRequestPtr& req, const IResponseWriterPtr& rsp) override;

private:
    TCallback<void(const IRequestPtr&, const IResponseWriterPtr&)> Handler_;
};

////////////////////////////////////////////////////////////////////////////////

struct IServer
    : public virtual TRefCounted
{
    //! Attaches a new handler.
    /*!
     *  Path matching semantic is copied from go standard library.
     *  See https://golang.org/pkg/net/http/#ServeMux
     */
    virtual void AddHandler(
        const TString& pattern,
        const IHttpHandlerPtr& handler) = 0;

    //! Returns the address this server listens at.
    virtual const NNet::TNetworkAddress& GetAddress() const = 0;

    //! Starts the server.
    virtual void Start() = 0;

    //! Stops the server.
    virtual void Stop() = 0;


    // Extension methods
    void AddHandler(
        const TString& pattern,
        TCallback<void(const IRequestPtr& req, const IResponseWriterPtr& rsp)> handler);
};

DEFINE_REFCOUNTED_TYPE(IServer)

////////////////////////////////////////////////////////////////////////////////

IServerPtr CreateServer(
    const TServerConfigPtr& config,
    const NNet::IListenerPtr& listener,
    const NConcurrency::IPollerPtr& poller);
IServerPtr CreateServer(
    const TServerConfigPtr& config,
    const NConcurrency::IPollerPtr& poller);
IServerPtr CreateServer(
    int port,
    const NConcurrency::IPollerPtr& poller);
IServerPtr CreateServer(
    const TServerConfigPtr& config);

////////////////////////////////////////////////////////////////////////////////

class TRequestPathMatcher
{
public:
    void Add(const TString& pattern, const IHttpHandlerPtr& handler);

    IHttpHandlerPtr Match(TStringBuf path);

private:
    THashMap<TString, IHttpHandlerPtr> Exact_;
    THashMap<TString, IHttpHandlerPtr> Subtrees_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NHttp
} // namespace NYT
