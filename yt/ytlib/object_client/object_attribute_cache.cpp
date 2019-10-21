#include "object_attribute_cache.h"

#include <yt/ytlib/cypress_client/object_attribute_fetcher.h>

namespace NYT::NObjectClient {

using namespace NApi;
using namespace NYTree;
using namespace NYPath;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

TObjectAttributeCache::TObjectAttributeCache(
    TObjectAttributeCacheConfigPtr config,
    std::vector<TString> attributes,
    NNative::IClientPtr client,
    IInvokerPtr invoker,
    NLogging::TLogger logger,
    NProfiling::TProfiler profiler)
    : TAsyncExpiringCache(config, std::move(profiler))
    , Config_(std::move(config))
    , Attributes_(std::move(attributes))
    , Logger(logger.AddTag("ObjectAttributeCacheId: %v", TGuid::Create()))
    , Client_(std::move(client))
    , Invoker_(std::move(invoker))
{ }

TFuture<TAttributeMap> TObjectAttributeCache::DoGet(const TYPath& key)
{
    return DoGetMany({key})
        .Apply(BIND([key] (const std::vector<TErrorOr<TAttributeMap>>& response) {
            return response[0].ValueOrThrow();
        }));
}

TFuture<std::vector<TErrorOr<TAttributeMap>>> TObjectAttributeCache::DoGetMany(const std::vector<TYPath>& keys)
{
    YT_LOG_DEBUG("Updating object attribute cache (KeyCount: %v)", keys.size());
    return NCypressClient::FetchAttributes(
        keys,
        Attributes_,
        Client_,
        TMasterReadOptions {
            Config_->ReadFrom,
            Config_->MasterCacheExpireAfterSuccessfulUpdateTime,
            Config_->MasterCacheExpireAfterFailedUpdateTime,
            Config_->MasterCacheStickyGroupSize,
        });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectClient
