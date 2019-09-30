#include "http_server_mock.h"

#include <yt/core/misc/assert.h>
#include <yt/core/misc/error.h>

namespace NYT::NTests {

////////////////////////////////////////////////////////////////////////////////

TString HttpResponse(int code, TString body)
{
    TString result;
    result += "HTTP/1.1 " + ToString(code) + " ";
    switch (code) {
        case 200: result += "Found"; break;
        case 404: result += "Not Found"; break;
        case 500: result += "Internal Server Error"; break;
        default: YT_ABORT();
    }
    result += "\r\n";
    result += "Connection: close\r\n";
    result += "Content-Length: " + ToString(body.length()) + "\r\n";
    result += "\r\n";
    result += body;
    return result;
}

TString CollectMessages(const TError& error)
{
    TString result;
    std::function<void(const TError&)> impl = [&] (const TError& e) {
        result += e.GetMessage();
        for (const auto& ie : e.InnerErrors()) {
            result += "\n";
            impl(ie);
        }
    };
    impl(error);
    return result;
}

////////////////////////////////////////////////////////////////////////////////

void THttpServerMock::SetCallback(TCallback callback)
{
    Callback_ = std::move(callback);
    if (ServerImpl_) {
        ServerImpl_->SetCallback(Callback_);
    }
}

void THttpServerMock::Start()
{
    YT_VERIFY(!IsStarted());

    ServerImpl_ = std::make_unique<THttpServerImpl>();
    ServerImpl_->SetCallback(Callback_);

    Server_ = std::make_unique<THttpServer>(
        ServerImpl_.get(),
        THttpServerOptions().SetHost("localhost"));
    Server_->Start();
}

void THttpServerMock::Stop()
{
    YT_VERIFY(IsStarted());

    Server_->Stop();
    Server_.reset();

    ServerImpl_.reset();
}

bool THttpServerMock::IsStarted() const
{
    return Server_.operator bool();
}

TString THttpServerMock::GetHost() const
{
    return Server_->Options().Host;
}

int THttpServerMock::GetPort() const
{
    return Server_->Options().Port;
}

////////////////////////////////////////////////////////////////////////////////

void THttpServerMock::THttpServerImpl::SetCallback(TCallback callback)
{
    Callback_ = std::move(callback);
}

TClientRequest* THttpServerMock::THttpServerImpl::CreateClient()
{
    return new TRequest(this);
}

////////////////////////////////////////////////////////////////////////////////

THttpServerMock::THttpServerImpl::TRequest::TRequest(THttpServerMock::THttpServerImpl* owner)
    : Owner_(owner)
{ }

bool THttpServerMock::THttpServerImpl::TRequest::Reply(void* /*opaque*/)
{
    if (!Owner_ || !Owner_->Callback_) {
        Output() << "HTTP/1.0 501 Not Implemented\r\n\r\n";
    } else {
        Owner_->Callback_(this);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTests
