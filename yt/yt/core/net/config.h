#pragma once

#include "public.h"

#include <yt/yt/core/ytree/yson_serializable.h>

#include <yt/yt/core/misc/cache_config.h>

namespace NYT::NNet {

////////////////////////////////////////////////////////////////////////////////

class TDialerConfig
    : public NYTree::TYsonSerializable
{
public:
    bool EnableNoDelay;
    bool EnableAggressiveReconnect;

    TDuration MinRto;
    TDuration MaxRto;
    double RtoScale;

    TDialerConfig();
};

DEFINE_REFCOUNTED_TYPE(TDialerConfig)

////////////////////////////////////////////////////////////////////////////////

//! Configuration for TAddressResolver singleton.
class TAddressResolverConfig
    : public TAsyncExpiringCacheConfig
{
public:
    bool EnableIPv4;
    bool EnableIPv6;
    //! If true, when determining local host name, it will additionally be resolved
    //! into FQDN by calling |getaddrinfo|. Setting this option to false may be
    //! useful in MTN environment, in which hostnames are barely resolvable.
    //! NB: Set this option to false only if you are sure that process is not being
    //! exposed under localhost name to anyone; in particular, any kind of discovery
    //! should be done using some other kind of addresses.
    bool ResolveHostNameIntoFqdn;
    //! If set, localhost name will be forcefully set to the given value rather
    //! than retrieved via |NYT::NNet::UpdateLocalHostName|.
    std::optional<TString> LocalHostNameOverride;
    int Retries;
    TDuration RetryDelay;
    TDuration ResolveTimeout;
    TDuration MaxResolveTimeout;
    double Jitter;
    TDuration WarningTimeout;

    TAddressResolverConfig();
};

DEFINE_REFCOUNTED_TYPE(TAddressResolverConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNet
