#include "abortable_http_response.h"

#include <util/system/mutex.h>
#include <util/generic/singleton.h>
#include <util/generic/hash_set.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TAbortableHttpResponseRegistry {
public:
    TOutageId StartOutage(TString urlPattern, const TOutageOptions& options)
    {
        auto g = Guard(Lock_);
        auto id = NextId_++;
        IdToOutage.emplace(id, TOutageEntry{std::move(urlPattern), options.ResponseCount_, options.LengthLimit_});
        return id;
    }

    void StopOutage(TOutageId id)
    {
        auto g = Guard(Lock_);
        IdToOutage.erase(id);
    }

    void Add(TAbortableHttpResponse* response)
    {
        auto g = Guard(Lock_);
        for (auto& [id, entry] : IdToOutage) {
            if (entry.Counter > 0 && response->GetUrl().find(entry.Pattern) != TString::npos) {
                response->SetLengthLimit(entry.LengthLimit);
                entry.Counter -= 1;
            }
        }
        ResponseList_.PushBack(response);
    }

    void Remove(TAbortableHttpResponse* response)
    {
        auto g = Guard(Lock_);
        response->Unlink();
    }

    static TAbortableHttpResponseRegistry& Get()
    {
        return *Singleton<TAbortableHttpResponseRegistry>();
    }

    int AbortAll(const TString& urlPattern)
    {
        int result = 0;
        for (auto& response : ResponseList_) {
            if (!response.IsAborted() && response.GetUrl().find(urlPattern) != TString::npos) {
                response.Abort();
                ++result;
            }
        }
        return result;
    }

private:
    struct TOutageEntry
    {
        TString Pattern;
        size_t Counter;
        size_t LengthLimit;
    };

private:
    TOutageId NextId_ = 0;
    TIntrusiveList<TAbortableHttpResponse> ResponseList_;
    THashMap<TOutageId, TOutageEntry> IdToOutage;
    TMutex Lock_;
};

////////////////////////////////////////////////////////////////////////////////

TAbortableHttpResponse::TOutage::TOutage(
    TString urlPattern,
    TAbortableHttpResponseRegistry& registry,
    const TOutageOptions& options)
    : UrlPattern_(std::move(urlPattern))
    , Registry_(registry)
    , Id_(registry.StartOutage(UrlPattern_, options))
{ }

TAbortableHttpResponse::TOutage::~TOutage()
{
    Stop();
}

void TAbortableHttpResponse::TOutage::Stop()
{
    if (!Stopped_) {
        Registry_.StopOutage(Id_);
        Stopped_ = true;
    }
}

////////////////////////////////////////////////////////////////////////////////

TAbortableHttpResponse::TAbortableHttpResponse(
    IInputStream* socketStream,
    const TString& requestId,
    const TString& hostName,
    const TString& url)
    : THttpResponse(socketStream, requestId, hostName)
    , Url_(url)
{
    TAbortableHttpResponseRegistry::Get().Add(this);
}

TAbortableHttpResponse::~TAbortableHttpResponse()
{
    TAbortableHttpResponseRegistry::Get().Remove(this);
}

size_t TAbortableHttpResponse::DoRead(void* buf, size_t len)
{
    if (Aborted_) {
        ythrow TAbortedForTestPurpose() << "response was aborted";
    }
    len = std::min(len, LengthLimit_);
    auto read = THttpResponse::DoRead(buf, len);
    LengthLimit_ -= read;
    if (LengthLimit_ == 0) {
        Abort();
    }
    return read;
}

size_t TAbortableHttpResponse::DoSkip(size_t len)
{
    if (Aborted_) {
        ythrow TAbortedForTestPurpose() << "response was aborted";
    }
    return THttpResponse::DoSkip(len);
}

void TAbortableHttpResponse::Abort()
{
    Aborted_ = true;
}

void TAbortableHttpResponse::SetLengthLimit(size_t limit)
{
    LengthLimit_ = limit;
    if (LengthLimit_ == 0) {
        Abort();
    }
}

int TAbortableHttpResponse::AbortAll(const TString& urlPattern)
{
    return TAbortableHttpResponseRegistry::Get().AbortAll(urlPattern);
}

TAbortableHttpResponse::TOutage TAbortableHttpResponse::StartOutage(
    const TString& urlPattern,
    const TOutageOptions& options)
{
    return TOutage(urlPattern, TAbortableHttpResponseRegistry::Get(), options);
}

TAbortableHttpResponse::TOutage TAbortableHttpResponse::StartOutage(
    const TString& urlPattern,
    size_t responseCount)
{
    return StartOutage(urlPattern, TOutageOptions().ResponseCount(responseCount));
}

const TString& TAbortableHttpResponse::GetUrl() const
{
    return Url_;
}

bool TAbortableHttpResponse::IsAborted() const
{
    return Aborted_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
