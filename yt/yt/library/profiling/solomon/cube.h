#pragma once

#include "tag_registry.h"

#include <yt/yt/library/profiling/sensor.h>
#include <yt/yt/library/profiling/summary.h>

#include <library/cpp/monlib/metrics/metric_consumer.h>

namespace NYT::NProfiling {

////////////////////////////////////////////////////////////////////////////////

struct TReadOptions
{
    std::vector<std::pair<std::vector<int>, TInstant>> Times;

    std::function<bool(const TString&)> SensorFilter;

    bool ConvertCountersToRateGauge = false;
    double RateDenominator = 1.0;

    bool EnableSolomonAggregationWorkaround = false;

    bool ExportSummaryAsMax = false;

    bool MarkAggregates = false;

    std::optional<TString> Host;

    std::vector<TTag> InstanceTags;

    bool Sparse = false;
    bool Global = false;
};

////////////////////////////////////////////////////////////////////////////////

template <class T>
class TCube
{
public:
    explicit TCube(int windowSize, i64 nextIteration);

    void Add(const TTagIdList& tagIds);
    void AddAll(const TTagIdList& tagIds, const TProjectionSet& projections);
    void Remove(const TTagIdList& tagIds);
    void RemoveAll(const TTagIdList& tagIds, const TProjectionSet& projections);

    void Update(const TTagIdList& tagIds, T value);

    void StartIteration();
    void FinishIteration();

    struct TProjection
    {
        T Rollup{};
        std::vector<T> Values;

        bool IsZero(int index) const;

        i64 LastUpdateIteration = 0;
        int UsageCount = 0;
    };

    const THashMap<TTagIdList, TCube::TProjection>& GetProjections() const;
    int GetSize() const;

    int GetIndex(i64 iteration) const;
    T Rollup(const TProjection& window, int index) const;

    void ReadSensors(
        const TString& name,
        const TReadOptions& options,
        const TTagRegistry& tagsRegistry,
        ::NMonitoring::IMetricConsumer* consumer) const;

private:
    int WindowSize_;
    i64 NextIteration_;
    int Index_ = 0;

    THashMap<TTagIdList, TProjection> Projections_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NProfiling
