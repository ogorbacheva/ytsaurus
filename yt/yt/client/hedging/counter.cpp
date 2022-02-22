#include "counter.h"

namespace NYT::NHedgingClient::NRpc {

static const auto HedgingClientProfiler = NYT::NProfiling::TRegistry{"/hedging_client"}.WithHot();
static const auto LagPenaltyProviderProfiler = NYT::NProfiling::TRegistry{"/lag_penalty_provider"}.WithHot();

TCounter::TCounter(const NYT::NProfiling::TRegistry& registry)
    : SuccessRequestCount(registry.Counter("/requests_success"))
    , CancelRequestCount(registry.Counter("/requests_cancel"))
    , ErrorRequestCount(registry.Counter("/requests_error"))
    , EffectivePenalty(registry.TimeGauge("/effective_penalty"))
    , ExternalPenalty(registry.TimeGauge("/external_penalty"))
    , RequestDuration(registry.Histogram("/request_duration", TDuration::MilliSeconds(1), TDuration::MilliSeconds(70)))
{
}

TCounter::TCounter(const TString& clusterName)
    : TCounter(HedgingClientProfiler.WithTag("yt_cluster", clusterName))
{
}

TCounter::TCounter(const NYT::NProfiling::TTagSet& tagSet)
    : TCounter(HedgingClientProfiler.WithTags(tagSet))
{
}

TLagPenaltyProviderCounters::TLagPenaltyProviderCounters(const NYT::NProfiling::TRegistry& registry, const TVector<TString>& clusters)
    : SuccessRequestCount(registry.Counter("/update_success"))
    , ErrorRequestCount(registry.Counter("/update_error"))
    , TotalTabletsCount(registry.Gauge("/tablets_total"))
{
    for (const auto& cluster : clusters) {
        LagTabletsCount.emplace(cluster, registry.WithTag("yt_cluster", cluster).Gauge("/tablets_with_lag"));
    }
}

TLagPenaltyProviderCounters::TLagPenaltyProviderCounters(const TString& tablePath, const TVector<TString>& clusterNames)
    : TLagPenaltyProviderCounters(LagPenaltyProviderProfiler.WithTag("table", tablePath), clusterNames)
{
}

} // namespace NYT::NHedgingClient::NRpc
