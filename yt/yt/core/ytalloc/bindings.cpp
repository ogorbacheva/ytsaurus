#include "bindings.h"
#include "config.h"

#include <yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>
#include <yt/yt/library/profiling/producer.h>

#include <yt/core/misc/singleton.h>
#include <yt/core/misc/string_builder.h>
#include <yt/core/misc/stack_trace.h>

#include <library/cpp/ytalloc/api/ytalloc.h>

#include <yt/core/ytree/yson_serializable.h>

#include <yt/core/libunwind/libunwind.h>

#include <util/system/env.h>

#include <cstdio>

namespace NYT::NYTAlloc {

using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

namespace {

const NLogging::TLogger& GetLogger()
{
    struct TSingleton
    {
        NLogging::TLogger Logger{"YTAlloc"};
    };

    return LeakySingleton<TSingleton>()->Logger;
}

NLogging::ELogLevel SeverityToLevel(NYTAlloc::ELogEventSeverity severity)
{
    switch (severity) {

        case ELogEventSeverity::Debug:   return NLogging::ELogLevel::Debug;
        case ELogEventSeverity::Info:    return NLogging::ELogLevel::Info;
        case ELogEventSeverity::Warning: return NLogging::ELogLevel::Warning;
        case ELogEventSeverity::Error:   return NLogging::ELogLevel::Error;
        default:                         Y_UNREACHABLE();
    }
}

void LogHandler(const NYTAlloc::TLogEvent& event)
{
    YT_LOG_EVENT(GetLogger(), SeverityToLevel(event.Severity), "%v", event.Message);
}

} // namespace

void EnableYTLogging()
{
    EnableLogging(LogHandler);
}

////////////////////////////////////////////////////////////////////////////////

class TProfilingStatisticsProducer
    : public NProfiling::ISensorProducer
{
public:
    TProfilingStatisticsProducer()
    {
        NProfiling::TRegistry registry{""};
        registry.AddProducer("/yt_alloc", MakeStrong(this));
    }

    virtual void Collect(NProfiling::ISensorWriter* writer) override
    {
        PushSystemAllocationStatistics(writer);
        PushTotalAllocationStatistics(writer);
        PushSmallAllocationStatistics(writer);
        PushLargeAllocationStatistics(writer);
        PushHugeAllocationStatistics(writer);
        PushUndumpableAllocationStatistics(writer);
        PushTimingStatistics(writer);
    }

private:
    template <class TCounters>
    static void PushAllocationCounterStatistics(
        NProfiling::ISensorWriter* writer,
        const TString& prefix,
        const TCounters& counters)
    {
        for (auto counter : TEnumTraits<typename TCounters::TIndex>::GetDomainValues()) {
            // TODO(prime@): Fix type.
            writer->AddGauge(prefix + "/" + FormatEnum(counter), counters[counter]);
        }
    }

    void PushSystemAllocationStatistics(NProfiling::ISensorWriter* writer)
    {
        auto counters = GetSystemAllocationCounters();
        PushAllocationCounterStatistics(writer, "/system", counters);
    }

    void PushTotalAllocationStatistics(NProfiling::ISensorWriter* writer)
    {
        auto counters = GetTotalAllocationCounters();
        PushAllocationCounterStatistics(writer, "/total", counters);
    }

    void PushHugeAllocationStatistics(NProfiling::ISensorWriter* writer)
    {
        auto counters = GetHugeAllocationCounters();
        PushAllocationCounterStatistics(writer, "/huge", counters);
    }

    void PushUndumpableAllocationStatistics(NProfiling::ISensorWriter* writer)
    {
        auto counters = GetUndumpableAllocationCounters();
        PushAllocationCounterStatistics(writer, "/undumpable", counters);
    }

    void PushSmallArenaStatistics(
        NProfiling::ISensorWriter* writer,
        size_t rank,
        const TEnumIndexedVector<ESmallArenaCounter, ssize_t>& counters)
    {
        writer->PushTag(std::pair<TString, TString>{"rank", ToString(rank)});
        PushAllocationCounterStatistics(writer, "/small_arena", counters);
        writer->PopTag();
    }

    void PushSmallAllocationStatistics(NProfiling::ISensorWriter* writer)
    {
        auto counters = GetSmallAllocationCounters();
        PushAllocationCounterStatistics(writer, "/small", counters);

        auto arenaCounters = GetSmallArenaAllocationCounters();
        for (size_t rank = 1; rank < SmallRankCount; ++rank) {
            PushSmallArenaStatistics(writer, rank, arenaCounters[rank]);
        }
    }

    void PushLargeArenaStatistics(
        NProfiling::ISensorWriter* writer,
        size_t rank,
        const TEnumIndexedVector<ELargeArenaCounter, ssize_t>& counters)
    {
        writer->PushTag(std::pair<TString, TString>{"rank", ToString(rank)});
        PushAllocationCounterStatistics(writer, "/large_arena", counters);
        writer->PopTag();

        auto bytesFreed = counters[ELargeArenaCounter::BytesFreed];
        auto bytesReleased = counters[ELargeArenaCounter::PagesReleased] * PageSize;
        int poolHitRatio;
        if (bytesFreed == 0) {
            poolHitRatio = 100;
        } else if (bytesReleased > bytesFreed) {
            poolHitRatio = 0;
        } else {
            poolHitRatio = 100 - bytesReleased * 100 / bytesFreed;
        }

        writer->AddGauge("/pool_hit_ratio", poolHitRatio);
    }

    void PushLargeAllocationStatistics(NProfiling::ISensorWriter* writer)
    {
        auto counters = GetLargeAllocationCounters();
        PushAllocationCounterStatistics(writer, "/large", counters);

        auto arenaCounters = GetLargeArenaAllocationCounters();
        for (size_t rank = MinLargeRank; rank < LargeRankCount; ++rank) {
            PushLargeArenaStatistics(writer, rank, arenaCounters[rank]);
        }
    }

    void PushTimingStatistics(NProfiling::ISensorWriter* writer)
    {
        auto timingEventCounters = GetTimingEventCounters();
        for (auto type : TEnumTraits<ETimingEventType>::GetDomainValues()) {
            const auto& counters = timingEventCounters[type];

            writer->PushTag(std::pair<TString, TString>{"type", ToString(type)});
            writer->AddGauge("/count", counters.Count);
            writer->AddGauge("/size", counters.Size);
            writer->PopTag();
        }
    }
};

void EnableYTProfiling()
{
    RefCountedSingleton<TProfilingStatisticsProducer>();
}

////////////////////////////////////////////////////////////////////////////////

void Configure(const TYTAllocConfigPtr& config)
{
    if (config->SmallArenasToProfile) {
        for (size_t rank = 1; rank < SmallRankCount; ++rank) {
            SetSmallArenaAllocationProfilingEnabled(rank, false);
        }
        for (auto rank : *config->SmallArenasToProfile) {
            if (rank < 1 || rank >= SmallRankCount) {
                THROW_ERROR_EXCEPTION("Unable to enable allocation profiling for small arena %v since its rank is out of range",
                    rank);
            }
            SetSmallArenaAllocationProfilingEnabled(rank, true);
        }
    }

    if (config->LargeArenasToProfile) {
        for (size_t rank = 1; rank < LargeRankCount; ++rank) {
            SetLargeArenaAllocationProfilingEnabled(rank, false);
        }
        for (auto rank : *config->LargeArenasToProfile) {
            if (rank < 1 || rank >= LargeRankCount) {
                THROW_ERROR_EXCEPTION("Unable to enable allocation profiling for large arena %v since its rank is out of range",
                    rank);
            }
            SetLargeArenaAllocationProfilingEnabled(rank, true);
        }
    }

    if (config->EnableAllocationProfiling) {
        SetAllocationProfilingEnabled(*config->EnableAllocationProfiling);
    }
    
    if (config->AllocationProfilingSamplingRate) {
        SetAllocationProfilingSamplingRate(*config->AllocationProfilingSamplingRate);
    }
    
    if (config->ProfilingBacktraceDepth) {
        SetProfilingBacktraceDepth(*config->ProfilingBacktraceDepth);
    }
    
    if (config->MinProfilingBytesUsedToReport) {
        SetMinProfilingBytesUsedToReport(*config->MinProfilingBytesUsedToReport);
    }
    
    if (config->StockpileInterval) {
        SetStockpileInterval(*config->StockpileInterval);
    }
    
    if (config->StockpileThreadCount) {
        SetStockpileThreadCount(*config->StockpileThreadCount);
    }
    
    if (config->StockpileSize) {
        SetStockpileSize(*config->StockpileSize);
    }
    
    if (config->EnableEagerMemoryRelease) {
        SetEnableEagerMemoryRelease(*config->EnableEagerMemoryRelease);
    }
    
    if (config->LargeUnreclaimableCoeff) {
        SetLargeUnreclaimableCoeff(*config->LargeUnreclaimableCoeff);
    }
    
    if (config->MinLargeUnreclaimableBytes) {
        SetMinLargeUnreclaimableBytes(*config->MinLargeUnreclaimableBytes);
    }
    
    if (config->MaxLargeUnreclaimableBytes) {
        SetMaxLargeUnreclaimableBytes(*config->MaxLargeUnreclaimableBytes);
    }
}

bool ConfigureFromEnv()
{
    const auto& Logger = GetLogger();

    static const TString ConfigEnvVarName = "YT_ALLOC_CONFIG";
    auto configVarValue = GetEnv(ConfigEnvVarName);
    if (!configVarValue) {
        YT_LOG_DEBUG("No %v environment variable is found",
            ConfigEnvVarName);
        return false;
    }

    TYTAllocConfigPtr config;
    try {
        config = ConvertTo<TYTAllocConfigPtr>(NYson::TYsonString(configVarValue));
    } catch (const std::exception& ex) {
        YT_LOG_ERROR(ex, "Error parsing environment variable %v",
            ConfigEnvVarName);
        return false;
    }

    YT_LOG_DEBUG("%v environment variable parsed successfully",
        ConfigEnvVarName);

    try {
        Configure(config);
    } catch (const std::exception& ex) {
        YT_LOG_ERROR(ex, "Error applying configuration parsed from environment variable");
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

void InitializeLibunwindInterop()
{
    SetBacktraceProvider(NLibunwind::GetStackTrace);
    SetBacktraceFormatter(FormatStackTrace);
}

TString FormatAllocationCounters()
{
    TStringBuilder builder;

    auto formatCounters = [&] (const auto& counters) {
        using T = typename std::decay_t<decltype(counters)>::TIndex;
        builder.AppendString("{");
        TDelimitedStringBuilderWrapper delimitedBuilder(&builder);
        for (auto counter : TEnumTraits<T>::GetDomainValues()) {
            delimitedBuilder->AppendFormat("%v: %v", counter, counters[counter]);
        }
        builder.AppendString("}");
    };

    builder.AppendString("Total = {");
    formatCounters(GetTotalAllocationCounters());

    builder.AppendString("}, System = {");
    formatCounters(GetSystemAllocationCounters());

    builder.AppendString("}, Small = {");
    formatCounters(GetSmallAllocationCounters());

    builder.AppendString("}, Large = {");
    formatCounters(GetLargeAllocationCounters());

    builder.AppendString("}, Huge = {");
    formatCounters(GetHugeAllocationCounters());

    builder.AppendString("}");
    return builder.Flush();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTAlloc
