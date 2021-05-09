#include <yt/yt/core/test_framework/framework.h>
#include <yt/yt/core/test_framework/test_key.h>

#include <yt/yt/core/http/server.h>
#include <yt/yt/core/http/client.h>
#include <yt/yt/core/http/private.h>
#include <yt/yt/core/http/http.h>
#include <yt/yt/core/http/stream.h>
#include <yt/yt/core/http/config.h>
#include <yt/yt/core/http/helpers.h>

#include <yt/yt/core/https/server.h>
#include <yt/yt/core/https/client.h>
#include <yt/yt/core/https/config.h>

#include <yt/yt/core/net/connection.h>
#include <yt/yt/core/net/listener.h>
#include <yt/yt/core/net/dialer.h>
#include <yt/yt/core/net/config.h>

#include <yt/yt/core/concurrency/poller.h>
#include <yt/yt/core/concurrency/thread_pool_poller.h>
#include <yt/yt/core/concurrency/async_stream.h>
#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/rpc/grpc/dispatcher.h>

#include <yt/yt/core/crypto/tls.h>

#include <yt/yt/core/misc/error.h>
#include <yt/yt/core/misc/finally.h>
#include <yt/yt/core/https/config.h>

#include <library/cpp/testing/common/network.h>

////////////////////////////////////////////////////////////////////////////////

namespace NYT::NHttp {
namespace {

using namespace NYT::NConcurrency;
using namespace NYT::NNet;
using namespace NYT::NCrypto;
using namespace NYT::NLogging;

////////////////////////////////////////////////////////////////////////////////

TEST(THttpUrlParse, Simple)
{
    TString example = "https://user@google.com:12345/a/b/c?foo=bar&zog=%20";
    auto url = ParseUrl(example);

    ASSERT_EQ(url.Protocol, TStringBuf("https"));
    ASSERT_EQ(url.Host, TStringBuf("google.com"));
    ASSERT_EQ(url.User, TStringBuf("user"));
    ASSERT_EQ(url.PortStr, TStringBuf("12345"));
    ASSERT_TRUE(url.Port);
    ASSERT_EQ(*url.Port, 12345);
    ASSERT_EQ(url.Path, TStringBuf("/a/b/c"));
    ASSERT_EQ(url.RawQuery, TStringBuf("foo=bar&zog=%20"));

    ASSERT_THROW(ParseUrl(TStringBuf("\0", 1)), TErrorException);
}

TEST(THttpUrlParse, IPv4)
{
    TString example = "https://1.2.3.4:12345/";
    auto url = ParseUrl(example);

    ASSERT_EQ(url.Host, TStringBuf("1.2.3.4"));
    ASSERT_EQ(*url.Port, 12345);
}

TEST(THttpUrlParse, IPv6)
{
    TString example = "https://[::1]:12345/";
    auto url = ParseUrl(example);

    ASSERT_EQ(url.Host, TStringBuf("::1"));
    ASSERT_EQ(*url.Port, 12345);
}

////////////////////////////////////////////////////////////////////////////////

TEST(THttpCookie, ParseCookie)
{
    TString cookieString = "yandexuid=706216621492423338; yandex_login=prime; _ym_d=1529669659; Cookie_check=1; _ym_isad=1;some_cookie_name= some_cookie_value ; abracadabra=";
    auto cookie = ParseCookies(cookieString);

    ASSERT_EQ("706216621492423338", cookie.at("yandexuid"));
    ASSERT_EQ("prime", cookie.at("yandex_login"));
    ASSERT_EQ("1529669659", cookie.at("_ym_d"));
    ASSERT_EQ("1", cookie.at("_ym_isad"));
    ASSERT_EQ("some_cookie_value", cookie.at("some_cookie_name"));
    ASSERT_EQ("", cookie.at("abracadabra"));
}

////////////////////////////////////////////////////////////////////////////////

std::vector<TString> ToVector(const SmallVector<TString, 1>& v)
{
    return std::vector<TString>(v.begin(), v.end());
}

TEST(THttpHeaders, Simple)
{
    auto headers = New<THeaders>();

    headers->Set("X-Test", "F");

    ASSERT_EQ(std::vector<TString>{{"F"}}, ToVector(headers->GetAll("X-Test")));
    ASSERT_EQ(TString{"F"}, headers->GetOrThrow("X-Test"));
    ASSERT_EQ(TString{"F"}, *headers->Find("X-Test"));

    ASSERT_THROW(headers->GetAll("X-Test2"), TErrorException);
    ASSERT_THROW(headers->GetOrThrow("X-Test2"), TErrorException);
    ASSERT_FALSE(headers->Find("X-Test2"));

    headers->Add("X-Test", "H");
    std::vector<TString> expected = {"F", "H"};
    ASSERT_EQ(expected, ToVector(headers->GetAll("X-Test")));

    headers->Set("X-Test", "J");
    ASSERT_EQ(std::vector<TString>{{"J"}}, ToVector(headers->GetAll("X-Test")));
}

TEST(THttpHeaders, HeaderCaseIsIrrelevant)
{
    auto headers = New<THeaders>();

    headers->Set("x-tEsT", "F");
    ASSERT_EQ(TString("F"), headers->GetOrThrow("x-test"));
    ASSERT_EQ(TString("F"), headers->GetOrThrow("X-Test"));

    TString buffer;
    TStringOutput output(buffer);
    headers->WriteTo(&output);

    TString expected = "x-tEsT: F\r\n";
    ASSERT_EQ(expected, buffer);
}


TEST(THttpHeaders, MessedUpHeaderValuesAreNotAllowed)
{
    auto headers = New<THeaders>();

    EXPECT_THROW(headers->Set("X-Newlines", "aaa\r\nbbb\nccc"), TErrorException);
    EXPECT_THROW(headers->Add("X-Newlines", "aaa\r\nbbb\nccc"), TErrorException);
}

////////////////////////////////////////////////////////////////////////////////

struct TFakeConnection
    : public IConnection
{
    TString Input;
    TString Output;

    virtual bool SetNoDelay() override
    {
        return true;
    }

    virtual bool SetKeepAlive() override
    {
        return true;
    }

    virtual TFuture<size_t> Read(const TSharedMutableRef& ref) override
    {
        size_t toCopy = std::min(ref.Size(), Input.size());
        std::copy_n(Input.data(), toCopy, ref.Begin());
        Input = Input.substr(toCopy);
        return MakeFuture(toCopy);
    }

    virtual TFuture<void> Write(const TSharedRef& ref) override
    {
        Output += TString(ref.Begin(), ref.Size());
        return VoidFuture;
    }

    virtual TFuture<void> WriteV(const TSharedRefArray& refs) override
    {
        for (const auto& ref : refs) {
            Output += TString(ref.Begin(), ref.Size());
        }
        return VoidFuture;
    }

    virtual TFuture<void> Close() override
    {
        THROW_ERROR_EXCEPTION("Not implemented");
    }

    virtual bool IsIdle() const override
    {
        return true;
    }

    virtual TFuture<void> Abort() override
    {
        THROW_ERROR_EXCEPTION("Not implemented");
    }

    virtual TFuture<void> CloseRead() override
    {
        THROW_ERROR_EXCEPTION("Not implemented");
    }

    virtual TFuture<void> CloseWrite() override
    {
        THROW_ERROR_EXCEPTION("Not implemented");
    }

    virtual const TNetworkAddress& LocalAddress() const override
    {
        THROW_ERROR_EXCEPTION("Not implemented");
    }

    virtual const TNetworkAddress& RemoteAddress() const override
    {
        THROW_ERROR_EXCEPTION("Not implemented");
    }

    virtual int GetHandle() const override
    {
        THROW_ERROR_EXCEPTION("Not implemented");
    }

    virtual TConnectionStatistics GetReadStatistics() const override
    {
        return {};
    }

    virtual TConnectionStatistics GetWriteStatistics() const override
    {
        return {};
    }

    virtual i64 GetReadByteCount() const override
    {
        return 0;
    }

    virtual i64 GetWriteByteCount() const override
    {
        return 0;
    }

    virtual void SetReadDeadline(std::optional<TInstant> /*deadline*/) override
    { }

    virtual void SetWriteDeadline(std::optional<TInstant> /*deadline*/) override
    { }

    virtual void SubscribePeerDisconnect(TCallback<void()> /*cb*/) override
    { }
};

DEFINE_REFCOUNTED_TYPE(TFakeConnection)

void FinishBody(THttpOutput* out)
{
    WaitFor(out->Close()).ThrowOnError();
}

void WriteChunk(THttpOutput* out, TStringBuf chunk)
{
    WaitFor(out->Write(TSharedRef::FromString(TString(chunk)))).ThrowOnError();
}

void WriteBody(THttpOutput* out, TStringBuf body)
{
    WaitFor(out->WriteBody(TSharedRef::FromString(TString(body)))).ThrowOnError();
}

TEST(THttpOutputTest, Full)
{
    typedef std::tuple<EMessageType, TString, std::function<void(THttpOutput*)>> TTestCase;
    std::vector<TTestCase> table = {
        TTestCase{
            EMessageType::Request,
            "GET / HTTP/1.1\r\n"
            "\r\n",
            [] (THttpOutput* out) {
                out->WriteRequest(EMethod::Get, "/");
                FinishBody(out);
            }
        },
        TTestCase{
            EMessageType::Request,
            "POST / HTTP/1.1\r\n"
            "Content-Length: 0\r\n"
            "\r\n",
            [] (THttpOutput* out) {
                out->WriteRequest(EMethod::Post, "/");
                FinishBody(out);
            }
        },
        TTestCase{
            EMessageType::Request,
            "POST / HTTP/1.1\r\n"
            "Content-Length: 1\r\n"
            "\r\n"
            "x",
            [] (THttpOutput* out) {
                out->WriteRequest(EMethod::Post, "/");
                WriteBody(out, TStringBuf("x"));
            }
        },
        TTestCase{
            EMessageType::Request,
            "POST / HTTP/1.1\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "1\r\n"
            "X\r\n"
            "A\r\n" // hex(10)
            "0123456789\r\n"
            "0\r\n"
            "\r\n",
            [] (THttpOutput* out) {
                out->WriteRequest(EMethod::Post, "/");

                WriteChunk(out, TStringBuf("X"));
                WriteChunk(out, TStringBuf("0123456789"));
                FinishBody(out);
            }
        },
        TTestCase{
            EMessageType::Response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 0\r\n"
            "\r\n",
            [] (THttpOutput* out) {
                out->SetStatus(EStatusCode::OK);
                FinishBody(out);
            }
        },
        TTestCase{
            EMessageType::Response,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 0\r\n"
            "X-YT-Response-Code: 500\r\n"
            "\r\n",
            [] (THttpOutput* out) {
                out->SetStatus(EStatusCode::BadRequest);
                out->GetTrailers()->Add("X-YT-Response-Code", "500");
                FinishBody(out);
            }
        },
        TTestCase{
            EMessageType::Response,
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Length: 4\r\n"
            "\r\n"
            "fail",
            [] (THttpOutput* out) {
                out->SetStatus(EStatusCode::InternalServerError);
                WriteBody(out, TStringBuf("fail"));
            }
        },
        TTestCase{
            EMessageType::Response,
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "1\r\n"
            "X\r\n"
            "A\r\n" // hex(10)
            "0123456789\r\n"
            "0\r\n"
            "\r\n",
            [] (THttpOutput* out) {
                out->SetStatus(EStatusCode::OK);

                WriteChunk(out, TStringBuf("X"));
                WriteChunk(out, TStringBuf("0123456789"));
                FinishBody(out);
            }
        },
    };

    for (auto [messageType, expected, callback]: table) {
        auto fake = New<TFakeConnection>();
        auto config = New<THttpIOConfig>();
        auto output = New<THttpOutput>(fake, messageType, config);

        try {
            callback(output.Get());
        } catch (const std::exception& ex) {
            ADD_FAILURE() << "Failed to write output"
                << expected
                << ex.what();
        }
        ASSERT_EQ(fake->Output, expected);
    }
}

TEST(THttpOutputTest, LargeResponse)
{
    constexpr ui64 Size = (4ULL << 30) + 1;
    const auto body = TString(Size, 'x');

    struct TLargeFakeConnection
        : public TFakeConnection
    {
        virtual TFuture<void> WriteV(const TSharedRefArray& refs) override
        {
            for (const auto& ref : refs) {
                if (ref.Size() == Size) {
                    LargeRef = ref;
                } else {
                    Output += TString(ref.Begin(), ref.Size());
                }
            }
            return VoidFuture;
        }

        TSharedRef LargeRef;
    };

    auto fake = New<TLargeFakeConnection>();
    auto config = New<THttpIOConfig>();
    auto output = New<THttpOutput>(fake, EMessageType::Response, config);

    output->SetStatus(EStatusCode::OK);
    WriteChunk(output.Get(), body);
    FinishBody(output.Get());

    // The large part is skipped and saved in LargeRef field.
    ASSERT_EQ(fake->Output,
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "100000001\r\n" // 4 GiB + 1B
        "\r\n"
        "0\r\n"
        "\r\n"
    );

    if (TStringBuf(fake->LargeRef.Begin(), fake->LargeRef.Size()) != body) {
        ADD_FAILURE() << "Wrong large chunk";
    }
}

////////////////////////////////////////////////////////////////////////////////


void ExpectBodyPart(THttpInput* in, TStringBuf chunk)
{
    ASSERT_EQ(chunk, ToString(WaitFor(in->Read()).ValueOrThrow()));
}

void ExpectBodyEnd(THttpInput* in)
{
    ASSERT_EQ(0u, WaitFor(in->Read()).ValueOrThrow().Size());
}

TEST(THttpInputTest, Simple)
{
    typedef std::tuple<EMessageType, TString, std::function<void(THttpInput*)>> TTestCase;
    std::vector<TTestCase> table = {
        TTestCase{
            EMessageType::Response,
            "HTTP/1.1 200 OK\r\n"
            "\r\n",
            [] (THttpInput* in) {
                EXPECT_EQ(in->GetStatusCode(), EStatusCode::OK);
                ExpectBodyEnd(in);
            }
        },
        TTestCase{
            EMessageType::Response,
            "HTTP/1.1 500 Internal Server Error\r\n"
            "\r\n",
            [] (THttpInput* in) {
                EXPECT_EQ(in->GetStatusCode(), EStatusCode::InternalServerError);
                ExpectBodyEnd(in);
            }
        },
        TTestCase{
            EMessageType::Request,
            "GET / HTTP/1.1\r\n"
            "\r\n",
            [] (THttpInput* in) {
                EXPECT_EQ(in->GetMethod(), EMethod::Get);
                EXPECT_EQ(in->GetUrl().Path, TStringBuf("/"));
                ExpectBodyEnd(in);
            }
        },
        TTestCase{
            EMessageType::Request,
            "GET / HTTP/1.1\r\n"
            "X-Foo: test\r\n"
            "X-Foo0: test-test-test\r\n"
            "X-FooFooFoo: test-test-test\r\n"
            "\r\n",
            [] (THttpInput* in) {
                EXPECT_EQ(in->GetMethod(), EMethod::Get);
                EXPECT_EQ(in->GetUrl().Path, TStringBuf("/"));
                auto headers = in->GetHeaders();

                ASSERT_EQ(TString("test"), headers->GetOrThrow("X-Foo"));
                ASSERT_EQ(TString("test-test-test"), headers->GetOrThrow("X-Foo0"));
                ASSERT_EQ(TString("test-test-test"), headers->GetOrThrow("X-FooFooFoo"));
                ExpectBodyEnd(in);
            }
        },
        TTestCase{
            EMessageType::Request,
            "POST / HTTP/1.1\r\n"
            "Content-Length: 6\r\n"
            "\r\n"
            "foobar",
            [] (THttpInput* in) {
                EXPECT_EQ(in->GetMethod(), EMethod::Post);
                ExpectBodyPart(in, "foobar");
                ExpectBodyEnd(in);
            }
        },
        TTestCase{
            EMessageType::Request,
            "POST /chunked_w_trailing_headers HTTP/1.1\r\n"
            "Transfer-Encoding: chunked\r\n"
            "X-Foo: test\r\n"
            "Connection: close\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "6\r\n world\r\n"
            "0\r\n"
            "Vary: *\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n",
            [] (THttpInput* in) {
                EXPECT_EQ(in->GetMethod(), EMethod::Post);
                EXPECT_EQ(in->GetUrl().Path, TStringBuf("/chunked_w_trailing_headers"));

                auto headers = in->GetHeaders();
                ASSERT_EQ(TString("test"), headers->GetOrThrow("X-Foo"));

                ASSERT_THROW(in->GetTrailers(), TErrorException);

                ExpectBodyPart(in, "hell");
                ExpectBodyPart(in, "o");
                ExpectBodyPart(in, " world");
                ExpectBodyEnd(in);

                auto trailers = in->GetTrailers();
                ASSERT_EQ(TString("*"), trailers->GetOrThrow("Vary"));
                ASSERT_EQ(TString("text/plain"), trailers->GetOrThrow("Content-Type"));
            }
        },
        TTestCase{
            EMessageType::Request,
            "GET http://yt/foo HTTP/1.1\r\n"
            "\r\n",
            [] (THttpInput* in) {
                EXPECT_EQ(TStringBuf("yt"), in->GetUrl().Host);
            }
        }
    };

    for (auto testCase : table) {
        auto fake = New<TFakeConnection>();
        fake->Input = std::get<1>(testCase);
        auto config = New<THttpIOConfig>();
        config->ReadBufferSize = 16;

        auto input = New<THttpInput>(fake, TNetworkAddress(), GetSyncInvoker(), std::get<0>(testCase), config);

        try {
            std::get<2>(testCase)(input.Get());
        } catch (const std::exception& ex) {
            ADD_FAILURE() << "Failed to parse input:"
                << std::endl << "==============" << std::endl
                << std::get<1>(testCase)
                << std::endl << "==============" << std::endl
                << ex.what();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

class THttpServerTest
    : public ::testing::TestWithParam<bool>
{
protected:
    IPollerPtr Poller;
    TServerConfigPtr ServerConfig;
    IServerPtr Server;
    IClientPtr Client;

    NTesting::TPortHolder TestPort;
    TString TestUrl;

private:
    void SetupServer(const NHttp::TServerConfigPtr& config)
    {
        config->Port = TestPort;
    }

    void SetupClient(const NHttp::TClientConfigPtr& /*config*/)
    { }

    virtual void SetUp() override
    {
        TestPort = NTesting::GetFreePort();
        TestUrl = Format("http://localhost:%v", TestPort);
        Poller = CreateThreadPoolPoller(4, "HttpTest");
        if (!GetParam()) {
            ServerConfig = New<NHttp::TServerConfig>();
            SetupServer(ServerConfig);
            Server = NHttp::CreateServer(ServerConfig, Poller);

            auto clientConfig = New<NHttp::TClientConfig>();
            SetupClient(clientConfig);
            Client = NHttp::CreateClient(clientConfig, Poller);
        } else {
            auto serverConfig = New<NHttps::TServerConfig>();
            serverConfig->Credentials = New<NHttps::TServerCredentialsConfig>();
            serverConfig->Credentials->PrivateKey = New<TPemBlobConfig>();
            serverConfig->Credentials->PrivateKey->Value = TestCertificate;
            serverConfig->Credentials->CertChain = New<TPemBlobConfig>();
            serverConfig->Credentials->CertChain->Value = TestCertificate;
            SetupServer(serverConfig);
            ServerConfig = serverConfig;
            Server = NHttps::CreateServer(serverConfig, Poller);

            auto clientConfig = New<NHttps::TClientConfig>();
            clientConfig->Credentials = New<NHttps::TClientCredentialsConfig>();
            clientConfig->Credentials->PrivateKey = New<TPemBlobConfig>();
            clientConfig->Credentials->PrivateKey->Value = TestCertificate;
            clientConfig->Credentials->CertChain = New<TPemBlobConfig>();
            clientConfig->Credentials->CertChain->Value = TestCertificate;
            SetupClient(clientConfig);
            Client = NHttps::CreateClient(clientConfig, Poller);
        }
    }

    virtual void TearDown() override
    {
        Server->Stop();
        Server.Reset();
        Poller->Shutdown();
        Poller.Reset();
        TestPort.Reset();
    }
};

class TOKHttpHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& rsp) override
    {
        rsp->SetStatus(EStatusCode::OK);
        WaitFor(rsp->Close()).ThrowOnError();
    }
};

TEST_P(THttpServerTest, SimpleRequest)
{
    Server->AddHandler("/ok", New<TOKHttpHandler>());
    Server->Start();

    auto rsp = WaitFor(Client->Get(TestUrl + "/ok")).ValueOrThrow();
    ASSERT_EQ(EStatusCode::OK, rsp->GetStatusCode());
}

class TEchoHttpHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& req, const IResponseWriterPtr& rsp) override
    {
        rsp->SetStatus(EStatusCode::OK);
        while (true) {
            auto data = WaitFor(req->Read()).ValueOrThrow();
            if (data.Size() == 0) {
                break;
            }
            WaitFor(rsp->Write(data)).ThrowOnError();
        }

        WaitFor(rsp->Close()).ThrowOnError();
    }
};

TString ReadAll(const IAsyncZeroCopyInputStreamPtr& in)
{
    TString buf;
    while (true) {
        auto data = WaitFor(in->Read()).ValueOrThrow();
        if (data.Size() == 0) {
            break;
        }

        buf += ToString(data);
    }

    return buf;
}


TEST_P(THttpServerTest, TransferSmallBody)
{
    Server->AddHandler("/echo", New<TEchoHttpHandler>());
    Server->Start();

    auto reqBody = TSharedMutableRef::Allocate(1024);
    std::fill(reqBody.Begin(), reqBody.End(), 0xab);

    auto rsp = WaitFor(Client->Post(TestUrl + "/echo", reqBody)).ValueOrThrow();
    ASSERT_EQ(EStatusCode::OK, rsp->GetStatusCode());

    auto rspBody = ReadAll(rsp);
    ASSERT_EQ(TString(reqBody.Begin(), reqBody.Size()), rspBody);

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

class TTestStatusCodeHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& rsp) override
    {
        rsp->SetStatus(Code);
        WaitFor(rsp->Close()).ThrowOnError();
    }

    EStatusCode Code = EStatusCode::OK;
};

TEST_P(THttpServerTest, StatusCode)
{
    auto handler = New<TTestStatusCodeHandler>();
    Server->AddHandler("/code", handler);
    Server->Start();

    handler->Code = EStatusCode::NotFound;
    ASSERT_EQ(EStatusCode::NotFound,
        WaitFor(Client->Get(TestUrl + "/code"))
            .ValueOrThrow()
            ->GetStatusCode());

    handler->Code = EStatusCode::Forbidden;
    ASSERT_EQ(EStatusCode::Forbidden,
        WaitFor(Client->Get(TestUrl + "/code"))
            .ValueOrThrow()
            ->GetStatusCode());

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

class TTestHeadersHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& req, const IResponseWriterPtr& rsp) override
    {
        for (const auto& header : ExpectedHeaders) {
            EXPECT_EQ(header.second, req->GetHeaders()->GetOrThrow(header.first));
        }

        for (const auto& header : ReplyHeaders) {
            rsp->GetHeaders()->Add(header.first, header.second);
        }

        rsp->SetStatus(EStatusCode::OK);
        WaitFor(rsp->Close()).ThrowOnError();
    }

    std::vector<std::pair<TString, TString>> ReplyHeaders, ExpectedHeaders;
};

TEST_P(THttpServerTest, HeadersTest)
{
    auto handler = New<TTestHeadersHandler>();
    handler->ExpectedHeaders = {
        { "X-Yt-Test", "foo; bar; zog" },
        { "Accept-Charset", "utf-8" }
    };
    handler->ReplyHeaders = {
        { "Content-Type", "test/plain; charset=utf-8" },
        { "Cache-Control", "nocache" }
    };

    Server->AddHandler("/headers", handler);
    Server->Start();

    auto headers = New<THeaders>();
    headers->Add("X-Yt-Test", "foo; bar; zog");
    headers->Add("Accept-Charset", "utf-8");

    auto rsp = WaitFor(Client->Get(TestUrl + "/headers", headers)).ValueOrThrow();
    EXPECT_EQ("nocache", rsp->GetHeaders()->GetOrThrow("Cache-Control"));
    EXPECT_EQ("test/plain; charset=utf-8", rsp->GetHeaders()->GetOrThrow("Content-Type"));

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

class TTestTrailersHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& rsp) override
    {
        WaitFor(rsp->Write(TSharedRef::FromString("test"))).ThrowOnError();

        rsp->GetTrailers()->Set("X-Yt-Test", "foo; bar");
        WaitFor(rsp->Close()).ThrowOnError();
    }
};

TEST_P(THttpServerTest, TrailersTest)
{
    auto handler = New<TTestTrailersHandler>();

    Server->AddHandler("/trailers", handler);
    Server->Start();

    auto rsp = WaitFor(Client->Get(TestUrl + "/trailers")).ValueOrThrow();
    auto body = ReadAll(rsp);
    EXPECT_EQ("foo; bar", rsp->GetTrailers()->GetOrThrow("X-Yt-Test"));

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

class THangingHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& /*rsp*/) override
    { }
};

class TImpatientHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& rsp) override
    {
        WaitFor(rsp->Write(TSharedRef::FromString("body"))).ThrowOnError();
        WaitFor(rsp->Close()).ThrowOnError();
    }
};

class TForgetfulHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& rsp) override
    {
        rsp->SetStatus(EStatusCode::OK);
    }
};

TEST_P(THttpServerTest, WierdHandlers)
{
    auto hanging = New<THangingHandler>();
    auto impatient = New<TImpatientHandler>();
    auto forgetful = New<TForgetfulHandler>();

    Server->AddHandler("/hanging", hanging);
    Server->AddHandler("/impatient", impatient);
    Server->AddHandler("/forgetful", forgetful);
    Server->Start();

    EXPECT_THROW(
        WaitFor(Client->Get(TestUrl + "/hanging"))
            .ValueOrThrow()
            ->GetStatusCode(),
        TErrorException);
    EXPECT_EQ(
        WaitFor(Client->Get(TestUrl + "/impatient"))
            .ValueOrThrow()
            ->GetStatusCode(),
        EStatusCode::InternalServerError);
    EXPECT_THROW(
        WaitFor(Client->Get(TestUrl + "/forgetful"))
            .ValueOrThrow()
            ->GetStatusCode(),
        TErrorException);

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

class TThrowingHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& /*rsp*/) override
    {
        THROW_ERROR_EXCEPTION("Your request is bad");
    }
};

TEST_P(THttpServerTest, ThrowingHandler)
{
    auto throwing = New<TThrowingHandler>();

    Server->AddHandler("/throwing", throwing);
    Server->Start();

    ASSERT_EQ(EStatusCode::InternalServerError,
        WaitFor(Client->Get(TestUrl + "/throwing"))
            .ValueOrThrow()
            ->GetStatusCode());

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

class TConsumingHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& req, const IResponseWriterPtr& rsp) override
    {
        while (WaitFor(req->Read()).ValueOrThrow().Size() != 0)
        { }

        rsp->SetStatus(EStatusCode::OK);
        WaitFor(rsp->Close()).ThrowOnError();
    }
};

TEST_P(THttpServerTest, RequestStreaming)
{
    Server->AddHandler("/consuming", New<TConsumingHandler>());
    Server->Start();

    auto body = TSharedMutableRef::Allocate(128 * 1024 * 1024);
    ASSERT_EQ(EStatusCode::OK,
        WaitFor(Client->Post(TestUrl + "/consuming", body))
            .ValueOrThrow()->GetStatusCode());

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

class TStreamingHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& rsp) override
    {
        rsp->SetStatus(EStatusCode::OK);
        auto data = TSharedRef::FromString(TString(1024, 'f'));
        for (int i = 0; i < 16 * 1024; i++) {
            WaitFor(rsp->Write(data))
                .ThrowOnError();
        }

        WaitFor(rsp->Close())
            .ThrowOnError();
    }
};

TEST_P(THttpServerTest, ResponseStreaming)
{
    Server->AddHandler("/streaming", New<TStreamingHandler>());
    Server->Start();

    auto rsp = WaitFor(Client->Get(TestUrl + "/streaming")).ValueOrThrow();
    ASSERT_EQ(16 * 1024 * 1024, std::ssize(ReadAll(rsp)));

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));
}

const auto& Logger = HttpLogger;

class TCancelingHandler
    : public IHttpHandler
{
public:
    TPromise<void> Canceled = NewPromise<void>();

    virtual void HandleRequest(const IRequestPtr& /*req*/, const IResponseWriterPtr& /*rsp*/) override
    {
        auto finally = Finally([this] {
            YT_LOG_DEBUG("Running finally block");
            Canceled.Set();
        });

        auto p = NewPromise<void>();
        p.OnCanceled(BIND([p] (const TError& error) {
            YT_LOG_INFO(error, "Promise is canceled");
            p.Set(error);
        }));

        YT_LOG_DEBUG("Blocking on promise");
        WaitFor(p.ToFuture())
            .ThrowOnError();
    }
};

TEST_P(THttpServerTest, RequestCancel)
{
    if (GetParam()) {
        return;
    }

    auto handler = New<TCancelingHandler>();

    ServerConfig->CancelFiberOnConnectionClose = true;
    Server->AddHandler("/cancel", handler);
    Server->Start();

    auto dialer = CreateDialer(New<TDialerConfig>(), Poller, HttpLogger);
    auto connection = WaitFor(dialer->Dial(TNetworkAddress::CreateIPv6Loopback(TestPort)))
        .ValueOrThrow();
    WaitFor(connection->Write(TSharedRef::FromString("POST /cancel HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n")))
        .ThrowOnError();

    Sleep(TDuration::Seconds(1));
    YT_LOG_DEBUG("Closing client connection");
    WaitFor(connection->CloseWrite())
        .ThrowOnError();

    WaitFor(handler->Canceled.ToFuture())
        .ThrowOnError();
}

class TValidateErrorHandler
    : public IHttpHandler
{
public:
    virtual void HandleRequest(const IRequestPtr& req, const IResponseWriterPtr& /*rsp*/) override
    {
        ASSERT_THROW(ReadAll(req), TErrorException);
        Ok = true;
    }

    bool Ok = false;
};

TEST_P(THttpServerTest, RequestHangUp)
{
    if (GetParam()) {
        // This test is not TLS-specific.
        return;
    }

    auto validating = New<TValidateErrorHandler>();
    Server->AddHandler("/validating", validating);
    Server->Start();

    auto dialer = CreateDialer(New<TDialerConfig>(), Poller, HttpLogger);
    auto connection = WaitFor(dialer->Dial(TNetworkAddress::CreateIPv6Loopback(TestPort)))
        .ValueOrThrow();
    WaitFor(connection->Write(TSharedRef::FromString("POST /validating HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n")))
        .ThrowOnError();
    WaitFor(connection->CloseWrite())
        .ThrowOnError();
    auto bytesRead = WaitFor(connection->Read(TSharedMutableRef::Allocate(1)))
        .ValueOrThrow();
    ASSERT_EQ(0u, bytesRead);

    Server->Stop();
    Sleep(TDuration::MilliSeconds(10));

    EXPECT_TRUE(validating->Ok);
}

TEST_P(THttpServerTest, ConnectionKeepAlive)
{
    if (GetParam()) {
        // This test is not TLS-specific.
        return;
    }

    Server->AddHandler("/echo", New<TEchoHttpHandler>());
    Server->Start();

    auto dialer = CreateDialer(New<TDialerConfig>(), Poller, HttpLogger);

    // Many requests.
    {
        auto connection = WaitFor(dialer->Dial(TNetworkAddress::CreateIPv6Loopback(TestPort)))
            .ValueOrThrow();

        auto request = New<THttpOutput>(
            connection,
            EMessageType::Request,
            New<THttpIOConfig>());

        auto response = New<THttpInput>(
            connection,
            connection->RemoteAddress(),
            Poller->GetInvoker(),
            EMessageType::Response,
            New<THttpIOConfig>());

        for (int i = 0; i < 10; ++i) {
            request->WriteRequest(EMethod::Post, "/echo");
            WaitFor(request->Write(TSharedRef::FromString("foo")))
                .ThrowOnError();
            WaitFor(request->Close())
                .ThrowOnError();

            response->GetStatusCode();
            auto body = response->ReadAll();

            ASSERT_TRUE(response->IsSafeToReuse());
            ASSERT_TRUE(request->IsSafeToReuse());
            response->Reset();
            request->Reset();
        }
    }

    // Pipelining
    {
        auto connection = WaitFor(dialer->Dial(TNetworkAddress::CreateIPv6Loopback(TestPort)))
            .ValueOrThrow();

        auto request = New<THttpOutput>(
            connection,
            EMessageType::Request,
            New<THttpIOConfig>());

        auto response = New<THttpInput>(
            connection,
            connection->RemoteAddress(),
            Poller->GetInvoker(),
            EMessageType::Response,
            New<THttpIOConfig>());

        for (int i = 0; i < 10; ++i) {
            request->WriteRequest(EMethod::Post, "/echo");
            WaitFor(request->Write(TSharedRef::FromString("foo")))
                .ThrowOnError();
            WaitFor(request->Close())
                .ThrowOnError();

            ASSERT_TRUE(request->IsSafeToReuse());
            request->Reset();
        }

        for (int i = 0; i < 10; ++i) {
            response->GetStatusCode();
            auto body = response->ReadAll();

            ASSERT_TRUE(response->IsSafeToReuse());
            response->Reset();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

INSTANTIATE_TEST_SUITE_P(WithoutTls, THttpServerTest, ::testing::Values(false));
INSTANTIATE_TEST_SUITE_P(WithTls, THttpServerTest, ::testing::Values(true));

////////////////////////////////////////////////////////////////////////////////

TEST(THttpServerTest, TestOwnPoller)
{
    auto port = NTesting::GetFreePort();
    auto url = Format("http://localhost:%v", port);

    auto config = New<NHttp::TServerConfig>();
    config->Port = port;
    auto server = NHttp::CreateServer(config);
    server->Start();
    server->Stop();
    // this test will cause memory leak w/o calling shutdown for IPoller in server
}

////////////////////////////////////////////////////////////////////////////////

TEST(THttpHandlerMatchingTest, Simple)
{
    auto h1 = New<TOKHttpHandler>();
    auto h2 = New<TOKHttpHandler>();
    auto h3 = New<TOKHttpHandler>();

    TRequestPathMatcher handlers;
    handlers.Add("/", h1);
    handlers.Add("/a", h2);
    handlers.Add("/a/b", h3);

    EXPECT_EQ(h1.Get(), handlers.Match(TStringBuf("/")).Get());
    EXPECT_EQ(h1.Get(), handlers.Match(TStringBuf("/c")).Get());

    EXPECT_EQ(h2.Get(), handlers.Match(TStringBuf("/a")).Get());
    EXPECT_EQ(h1.Get(), handlers.Match(TStringBuf("/a/")).Get());

    EXPECT_EQ(h3.Get(), handlers.Match(TStringBuf("/a/b")).Get());
    EXPECT_EQ(h1.Get(), handlers.Match(TStringBuf("/a/b/")).Get());

    TRequestPathMatcher handlers2;
    handlers2.Add("/a/", h2);
    EXPECT_FALSE(handlers2.Match(TStringBuf("/")).Get());
    EXPECT_EQ(h2.Get(), handlers2.Match(TStringBuf("/a")).Get());
    EXPECT_EQ(h2.Get(), handlers2.Match(TStringBuf("/a/")).Get());
    EXPECT_EQ(h2.Get(), handlers2.Match(TStringBuf("/a/b")).Get());

    TRequestPathMatcher handlers3;
    handlers3.Add("/a/", h2);
    handlers3.Add("/a", h3);

    EXPECT_EQ(h3.Get(), handlers3.Match(TStringBuf("/a")).Get());
    EXPECT_EQ(h2.Get(), handlers3.Match(TStringBuf("/a/")).Get());
    EXPECT_EQ(h2.Get(), handlers3.Match(TStringBuf("/a/b")).Get());
}

////////////////////////////////////////////////////////////////////////////////

TEST(TRangeHeadersTest, Test)
{
    auto headers = New<THeaders>();
    EXPECT_EQ(GetRange(headers), std::nullopt);

    headers->Set("Range", "bytes=0-1234");

    std::pair<int64_t, int64_t> result{0, 1234};
    EXPECT_EQ(GetRange(headers), result);

    headers->Set("Range", "bytes=junk");
    EXPECT_ANY_THROW(GetRange(headers));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NHttp
