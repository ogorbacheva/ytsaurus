#pragma once

#include "public.h"

#include <yt/yt/core/ytree/yson_struct.h>

namespace NYT::NContainers {

////////////////////////////////////////////////////////////////////////////////

struct TDiskManagerProxyConfig
    : public NYTree::TYsonStruct
{
    TString DiskManagerAddress;
    TString DiskManagerServiceName;

    TDuration RequestTimeout;

    REGISTER_YSON_STRUCT(TDiskManagerProxyConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TDiskManagerProxyConfig)

////////////////////////////////////////////////////////////////////////////////

struct TDiskManagerProxyDynamicConfig
    : public NYTree::TYsonStruct
{
    std::optional<TDuration> RequestTimeout;

    REGISTER_YSON_STRUCT(TDiskManagerProxyDynamicConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TDiskManagerProxyDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NContainers
