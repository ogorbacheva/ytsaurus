#pragma once

#include "public.h"
#include "subject.h"

#include <yt/yt/server/master/cell_master/public.h>

#include <yt/yt/server/master/object_server/object.h>

#include <yt/yt/core/yson/consumer.h>

#include <yt/yt/core/misc/property.h>

#include <yt/yt/core/concurrency/public.h>

namespace NYT::NSecurityServer {

////////////////////////////////////////////////////////////////////////////////

struct TUserWorkloadStatistics
{
    std::atomic<i64> RequestCount = 0;

    //! Total request time in milliseconds.
    std::atomic<i64> RequestTime = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TUserRequestLimitsOptions
    : public NYTree::TYsonStruct
{
public:
    std::optional<int> Default;
    THashMap<NObjectServer::TCellTag, int> PerCell;

    void SetValue(NObjectServer::TCellTag cellTag, std::optional<int> value);
    std::optional<int> GetValue(NObjectServer::TCellTag cellTag) const;

    REGISTER_YSON_STRUCT(TUserRequestLimitsOptions);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TUserRequestLimitsOptions)

////////////////////////////////////////////////////////////////////////////////

class TUserQueueSizeLimitsOptions
    : public NYTree::TYsonStruct
{
public:
    int Default;
    THashMap<NObjectServer::TCellTag, int> PerCell;

    void SetValue(NObjectServer::TCellTag cellTag, int value);
    int GetValue(NObjectServer::TCellTag cellTag) const;

    REGISTER_YSON_STRUCT(TUserQueueSizeLimitsOptions);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TUserQueueSizeLimitsOptions)

////////////////////////////////////////////////////////////////////////////////

class TUserRequestLimitsConfig
    : public NYTree::TYsonStruct
{
public:
    TUserRequestLimitsOptionsPtr ReadRequestRateLimits;
    TUserRequestLimitsOptionsPtr WriteRequestRateLimits;
    TUserQueueSizeLimitsOptionsPtr RequestQueueSizeLimits;

    REGISTER_YSON_STRUCT(TUserRequestLimitsConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TUserRequestLimitsConfig)


////////////////////////////////////////////////////////////////////////////////

class TSerializableUserRequestLimitsOptions
    : public NYTree::TYsonStruct
{
public:
    TUserRequestLimitsOptionsPtr ToLimitsOrThrow(const NCellMaster::IMulticellManagerPtr& multicellManager) const;

    static TSerializableUserRequestLimitsOptionsPtr CreateFrom(
        const TUserRequestLimitsOptionsPtr& options,
        const NCellMaster::IMulticellManagerPtr& multicellManager);

    REGISTER_YSON_STRUCT(TSerializableUserRequestLimitsOptions);

    static void Register(TRegistrar registrar);

private:
    std::optional<int> Default_;
    THashMap<TString, int> PerCell_;
};

DEFINE_REFCOUNTED_TYPE(TSerializableUserRequestLimitsOptions)

////////////////////////////////////////////////////////////////////////////////

class TSerializableUserQueueSizeLimitsOptions
    : public NYTree::TYsonStruct
{
public:
    static TSerializableUserQueueSizeLimitsOptionsPtr CreateFrom(
        const TUserQueueSizeLimitsOptionsPtr& options,
        const NCellMaster::IMulticellManagerPtr& multicellManager);

    TUserQueueSizeLimitsOptionsPtr ToLimitsOrThrow(const NCellMaster::IMulticellManagerPtr& multicellManager) const;

    REGISTER_YSON_STRUCT(TSerializableUserQueueSizeLimitsOptions);

    static void Register(TRegistrar registrar);

private:
    int Default_;
    THashMap<TString, int> PerCell_;
};

DEFINE_REFCOUNTED_TYPE(TSerializableUserQueueSizeLimitsOptions)

////////////////////////////////////////////////////////////////////////////////

class TSerializableUserRequestLimitsConfig
    : public NYTree::TYsonStruct
{
public:
    static TSerializableUserRequestLimitsConfigPtr CreateFrom(
        const TUserRequestLimitsConfigPtr& config,
        const NCellMaster::IMulticellManagerPtr& multicellManager);

    TUserRequestLimitsConfigPtr ToConfigOrThrow(const NCellMaster::IMulticellManagerPtr& multicellManager) const;

    REGISTER_YSON_STRUCT(TSerializableUserRequestLimitsConfig);

    static void Register(TRegistrar registrar);

private:
    TSerializableUserRequestLimitsOptionsPtr ReadRequestRateLimits_;
    TSerializableUserRequestLimitsOptionsPtr WriteRequestRateLimits_;
    TSerializableUserQueueSizeLimitsOptionsPtr RequestQueueSizeLimits_;
};

DEFINE_REFCOUNTED_TYPE(TSerializableUserRequestLimitsConfig)

////////////////////////////////////////////////////////////////////////////////

class TUser
    : public TSubject
{
public:
    // Limits and bans.
    DEFINE_BYVAL_RW_PROPERTY(bool, Banned);
    DEFINE_BYVAL_RW_PROPERTY(TUserRequestLimitsConfigPtr, RequestLimits);

    int GetRequestQueueSize() const;
    void SetRequestQueueSize(int size);
    void ResetRequestQueueSize();

    using TStatistics = TEnumIndexedVector<EUserWorkloadType, TUserWorkloadStatistics>;
    DEFINE_BYREF_RW_PROPERTY(TStatistics, Statistics);

public:
    using TSubject::TSubject;
    explicit TUser(TUserId id);

    TString GetLowercaseObjectName() const override;
    TString GetCapitalizedObjectName() const override;

    void Save(NCellMaster::TSaveContext& context) const;
    void Load(NCellMaster::TLoadContext& context);

    const NConcurrency::IReconfigurableThroughputThrottlerPtr& GetRequestRateThrottler(EUserWorkloadType workloadType);
    void SetRequestRateThrottler(NConcurrency::IReconfigurableThroughputThrottlerPtr throttler, EUserWorkloadType workloadType);

    std::optional<int> GetRequestRateLimit(EUserWorkloadType workloadType, NObjectServer::TCellTag cellTag = NObjectClient::InvalidCellTag) const;
    void SetRequestRateLimit(std::optional<int> limit, EUserWorkloadType workloadType, NObjectServer::TCellTag cellTag = NObjectClient::InvalidCellTag);

    int GetRequestQueueSizeLimit(NObjectServer::TCellTag cellTag = NObjectClient::InvalidCellTag) const;
    void SetRequestQueueSizeLimit(int limit, NObjectServer::TCellTag cellTag = NObjectClient::InvalidCellTag);

    void UpdateCounters(const TUserWorkload& workloadType);

protected:
    // Transient
    int RequestQueueSize_ = 0;

private:
    NConcurrency::IReconfigurableThroughputThrottlerPtr ReadRequestRateThrottler_;
    NConcurrency::IReconfigurableThroughputThrottlerPtr WriteRequestRateThrottler_;

    NProfiling::TTimeCounter ReadTimeCounter_;
    NProfiling::TTimeCounter WriteTimeCounter_;
    NProfiling::TCounter RequestCounter_;
    NProfiling::TCounter ReadRequestCounter_;
    NProfiling::TCounter WriteRequestCounter_;
    NProfiling::TSummary RequestQueueSizeSummary_;
};

DEFINE_MASTER_OBJECT_TYPE(TUser)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityServer
