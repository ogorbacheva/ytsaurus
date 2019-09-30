#pragma once

#include "public.h"

#include <yt/ytlib/api/native/public.h>

#include <yt/core/ytree/yson_serializable.h>

namespace NYT::NCellMasterClient {

///////////////////////////////////////////////////////////////////////////////

class TCellDirectoryConfig
    : public virtual NYTree::TYsonSerializable
{
public:
    NApi::NNative::TMasterConnectionConfigPtr PrimaryMaster;
    std::vector<NApi::NNative::TMasterConnectionConfigPtr> SecondaryMasters;
    NApi::NNative::TMasterConnectionConfigPtr MasterCache;

    TCellDirectoryConfig();
};

DEFINE_REFCOUNTED_TYPE(TCellDirectoryConfig)

////////////////////////////////////////////////////////////////////////////////

class TCellDirectorySynchronizerConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Interval between subsequent directory updates.
    std::optional<TDuration> SyncPeriod;

    TDuration SuccessExpirationTime;
    TDuration FailureExpirationTime;

    TCellDirectorySynchronizerConfig();
};

DEFINE_REFCOUNTED_TYPE(TCellDirectorySynchronizerConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellMasterClient
