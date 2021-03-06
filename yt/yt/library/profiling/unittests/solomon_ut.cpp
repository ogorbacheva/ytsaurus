#include "yt/yt/core/misc/ref_counted.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <yt/yt/library/profiling/sensor.h>
#include <yt/yt/library/profiling/impl.h>
#include <yt/yt/library/profiling/producer.h>

#include <yt/yt/library/profiling/solomon/registry.h>

#include <util/string/join.h>

namespace NYT::NProfiling {
namespace {

////////////////////////////////////////////////////////////////////////////////

struct TTestMetricConsumer
    : public NMonitoring::IMetricConsumer
{
    virtual void OnStreamBegin() override
    { }

    virtual void OnStreamEnd() override
    { }

    virtual void OnCommonTime(TInstant) override
    { }

    virtual void OnMetricBegin(NMonitoring::EMetricType) override
    { }

    virtual void OnMetricEnd() override
    { }

    virtual void OnLabelsBegin() override
    {
        Labels.clear();
    }

    virtual void OnLabelsEnd() override
    { }

    virtual void OnLabel(TStringBuf name, TStringBuf value)
    {
        if (name == "sensor") {
            Name = value;
        } else {
            Labels.emplace_back(TString(name) + "=" + value);
        }
    }

    virtual void OnDouble(TInstant, double value)
    {
        Cerr << FormatName() << " " << value << Endl;
        Gauges[FormatName()] = value;
    }

    virtual void OnUint64(TInstant, ui64)
    { }

    virtual void OnInt64(TInstant, i64 value)
    {
        Cerr << FormatName() << " " << value << Endl;
        Counters[FormatName()] = value;
    }

    virtual void OnHistogram(TInstant, NMonitoring::IHistogramSnapshotPtr value) override
    {
        Cerr << FormatName() << " historgram{";
        for (size_t i = 0; i < value->Count(); ++i) {
            Cerr << value->Value(i) << ",";
        }
        Cerr << "}" << Endl;
        Histograms[FormatName()] = value;
    }

    virtual void OnLogHistogram(TInstant, NMonitoring::TLogHistogramSnapshotPtr) override
    { }

    virtual void OnSummaryDouble(TInstant, NMonitoring::ISummaryDoubleSnapshotPtr) override
    { }

    TString Name;
    std::vector<TString> Labels;

    THashMap<TString, i64> Counters;
    THashMap<TString, double> Gauges;
    THashMap<TString, NMonitoring::IHistogramSnapshotPtr> Histograms;

    TString FormatName() const
    {
        return Name + "{" + JoinSeq(";", Labels) + "}";
    }
};

////////////////////////////////////////////////////////////////////////////////

TEST(TSolomonRegistry, Registration)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/debug");

    auto counter = registry.Counter("/c0");
    auto gauge = registry.Gauge("/g0");

    impl->ProcessRegistrations();

    counter.Increment(1);
    gauge.Update(42);
}

TTestMetricConsumer CollectSensors(TSolomonRegistryPtr impl, int subsample = 1, bool enableHack = false)
{
    impl->ProcessRegistrations();

    auto i = impl->GetNextIteration();
    impl->Collect();

    TTestMetricConsumer testConsumer;

    TReadOptions options;
    options.EnableSolomonAggregationWorkaround = enableHack;
    options.Times = {{{}, TInstant::Now()}};
    for (int j = subsample - 1; j >= 0; --j) {
        options.Times[0].first.push_back(impl->IndexOf(i - j));
    }

    impl->ReadSensors(options, &testConsumer);
    Cerr << "-------------------------------------" << Endl;

    return testConsumer;
}

TEST(TSolomonRegistry, CounterProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto c0 = registry.WithTag("user", "u0").Counter("/count");
    auto c1 = registry.WithTag("user", "u1").Counter("/count");

    auto result = CollectSensors(impl).Counters;

    ASSERT_EQ(result["yt.d.count{}"], 0u);
    ASSERT_EQ(result["yt.d.count{user=u0}"], 0u);

    c0.Increment();
    c1.Increment();

    result = CollectSensors(impl).Counters;

    ASSERT_EQ(result["yt.d.count{}"], 2u);
    ASSERT_EQ(result["yt.d.count{user=u0}"], 1u);

    c0.Increment();
    c1 = {};

    result = CollectSensors(impl).Counters;
    ASSERT_EQ(result["yt.d.count{}"], 3u);
    ASSERT_EQ(result["yt.d.count{user=u0}"], 2u);
    ASSERT_EQ(result.find("yt.d.count{user=u1}"), result.end());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, GaugeProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto g0 = registry.WithTag("user", "u0").Gauge("/memory");
    auto g1 = registry.WithTag("user", "u1").Gauge("/memory");

    auto result = CollectSensors(impl).Gauges;

    ASSERT_EQ(result["yt.d.memory{}"], 0.0);
    ASSERT_EQ(result["yt.d.memory{user=u0}"], 0.0);

    g0.Update(1.0);
    g1.Update(2.0);

    result = CollectSensors(impl).Gauges;
    ASSERT_EQ(result["yt.d.memory{}"], 3.0);
    ASSERT_EQ(result["yt.d.memory{user=u0}"], 1.0);

    g0.Update(10.0);
    g1 = {};

    result = CollectSensors(impl).Gauges;
    ASSERT_EQ(result["yt.d.memory{}"], 10.0);
    ASSERT_EQ(result["yt.d.memory{user=u0}"], 10.0);
    ASSERT_EQ(result.find("yt.d.memory{user=u1}"), result.end());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, ExponentialHistogramProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto c0 = registry.WithTag("user", "u0").Histogram("/histogram", TDuration::Zero(), TDuration::MilliSeconds(20));
    auto c1 = registry.WithTag("user", "u1").Histogram("/histogram", TDuration::Zero(), TDuration::MilliSeconds(20));

    auto result = CollectSensors(impl).Histograms;

    ASSERT_EQ(result["yt.d.histogram{}"]->Count(), 16u);
    ASSERT_EQ(result["yt.d.histogram{user=u0}"]->Count(), 16u);

    c0.Record(TDuration::MilliSeconds(5));
    c1.Record(TDuration::MilliSeconds(5));
    c0.Record(TDuration::MilliSeconds(30));

    result = CollectSensors(impl).Histograms;

    ASSERT_EQ(result["yt.d.histogram{}"]->Count(), 16u);
    ASSERT_EQ(result["yt.d.histogram{}"]->Value(13), 2u);
    ASSERT_EQ(result["yt.d.histogram{user=u0}"]->Value(13), 1u);

    ASSERT_EQ(result["yt.d.histogram{}"]->Value(15), 1u);
    ASSERT_EQ(Max<double>(), result["yt.d.histogram{}"]->UpperBound(15));

    c0.Record(TDuration::MilliSeconds(10));
    c1 = {};

    result = CollectSensors(impl).Histograms;
    ASSERT_EQ(result["yt.d.histogram{}"]->Value(14), 1u);
    ASSERT_EQ(result["yt.d.histogram{user=u0}"]->Value(14), 1u);
    ASSERT_EQ(result.find("yt.d.histogram{user=u1}"), result.end());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, CustomHistogramProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    std::vector<TDuration> bounds{
        TDuration::Zero(), TDuration::MilliSeconds(5), TDuration::MilliSeconds(10), TDuration::MilliSeconds(15)
    };
    auto c0 = registry.WithTag("user", "u0").Histogram("/histogram", bounds);
    auto c1 = registry.WithTag("user", "u1").Histogram("/histogram", bounds);

    auto result = CollectSensors(impl).Histograms;

    ASSERT_EQ(result["yt.d.histogram{}"]->Count(), 5u);
    ASSERT_EQ(result["yt.d.histogram{user=u0}"]->Count(), 5u);

    c0.Record(TDuration::MilliSeconds(5));
    c1.Record(TDuration::MilliSeconds(5));
    c0.Record(TDuration::MilliSeconds(16));

    result = CollectSensors(impl).Histograms;

    ASSERT_EQ(result["yt.d.histogram{}"]->Count(), 5u);
    ASSERT_EQ(result["yt.d.histogram{}"]->Value(1), 2u);
    ASSERT_EQ(result["yt.d.histogram{user=u0}"]->Value(1), 1u);

    ASSERT_EQ(result["yt.d.histogram{}"]->Value(4), 1u);
    ASSERT_EQ(Max<double>(), result["yt.d.histogram{}"]->UpperBound(4));

    c0.Record(TDuration::MilliSeconds(10));
    c1 = {};

    result = CollectSensors(impl).Histograms;
    ASSERT_EQ(result["yt.d.histogram{}"]->Value(2), 1u);
    ASSERT_EQ(result["yt.d.histogram{user=u0}"]->Value(2), 1u);
    ASSERT_EQ(result.find("yt.d.histogram{user=u1}"), result.end());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, SparseHistogram)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto h0 = registry.WithSparse().Histogram("/histogram", TDuration::Zero(), TDuration::MilliSeconds(20));

    auto result = CollectSensors(impl).Histograms;
    ASSERT_TRUE(result.empty());

    h0.Record(TDuration::MilliSeconds(5));
    result = CollectSensors(impl).Histograms;

    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result["yt.d.histogram{}"]->Count(), 16u);
    ASSERT_EQ(result["yt.d.histogram{}"]->Value(13), 1u);

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, SparseCounters)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto c = registry.WithSparse().Counter("/sparse_counter");

    auto result = CollectSensors(impl).Counters;
    ASSERT_TRUE(result.empty());

    c.Increment();
    result = CollectSensors(impl).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter{}"], 1u);

    result = CollectSensors(impl).Counters;
    ASSERT_TRUE(result.empty());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);

    c.Increment();
    result = CollectSensors(impl).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter{}"], 2u);
}

TEST(TSolomonRegistry, GaugesNoDefault)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto g = registry.WithDefaultDisabled().Gauge("/gauge");

    auto result = CollectSensors(impl).Gauges;
    ASSERT_TRUE(result.empty());

    g.Update(1);
    result = CollectSensors(impl).Gauges;
    ASSERT_EQ(result["yt.d.gauge{}"], 1.0);
}

TEST(TSolomonRegistry, SparseCountersWithHack)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto c = registry.WithSparse().Counter("/sparse_counter_with_hack");

    auto result = CollectSensors(impl, 1, true).Counters;
    ASSERT_TRUE(result.empty());

    c.Increment();
    result = CollectSensors(impl, 1, true).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter_with_hack{}"], 1u);

    result = CollectSensors(impl, 2, true).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter_with_hack{}"], 1u);

    result = CollectSensors(impl, 3, true).Counters;
    ASSERT_EQ(result["yt.d.sparse_counter_with_hack{}"], 1u);

    result = CollectSensors(impl, 3, true).Counters;
    ASSERT_TRUE(result.empty());
}

TEST(TSolomonRegistry, SparseGauge)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler registry(impl, "/d");

    auto c = registry.WithSparse().Gauge("/sparse_gauge");

    auto result = CollectSensors(impl).Gauges;
    ASSERT_TRUE(result.empty());

    c.Update(1.0);
    result = CollectSensors(impl).Gauges;
    ASSERT_EQ(result["yt.d.sparse_gauge{}"], 1.0);

    c.Update(0.0);
    result = CollectSensors(impl).Gauges;
    ASSERT_TRUE(result.empty());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, InvalidSensors)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d");

    auto invalidTypeCounter = r.Counter("/invalid_type");
    auto invalidTypeGauge = r.Gauge("/invalid_type");

    auto invalidSettingsCounter0 = r.Counter("/invalid_settings");
    auto invalidSettingsCounter1 = r.WithGlobal().Counter("/invalid_settings");

    auto result = CollectSensors(impl);
    ASSERT_TRUE(result.Counters.empty());
    ASSERT_TRUE(result.Gauges.empty());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

struct TDebugProducer
    : public ISensorProducer
{
    TSensorBuffer Buffer;

    virtual ~TDebugProducer()
    { }

    virtual void CollectSensors(ISensorWriter* writer) override
    {
        Buffer.WriteTo(writer);
    }
};

TEST(TSolomonRegistry, GaugeProducer)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d");

    auto p0 = New<TDebugProducer>();
    r.AddProducer("/cpu", p0);

    auto p1 = New<TDebugProducer>();
    r.AddProducer("/cpu", p1);

    auto result = CollectSensors(impl).Gauges;
    ASSERT_TRUE(result.empty());

    p0->Buffer.PushTag(std::pair<TString, TString>{"thread", "Control"});
    p0->Buffer.AddGauge("/user_time", 98);
    p0->Buffer.AddGauge("/system_time", 15);

    p1->Buffer.PushTag(std::pair<TString, TString>{"thread", "Profiler"});
    p1->Buffer.AddGauge("/user_time", 2);
    p1->Buffer.AddGauge("/system_time", 25);

    result = CollectSensors(impl).Gauges;
    ASSERT_EQ(result["yt.d.cpu.user_time{thread=Control}"], 98.0);
    ASSERT_EQ(result["yt.d.cpu.user_time{thread=Profiler}"], 2.0);
    ASSERT_EQ(result["yt.d.cpu.user_time{}"], 100.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{thread=Control}"], 15.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{thread=Profiler}"], 25.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{}"], 40.0);

    p0 = {};
    result = CollectSensors(impl).Gauges;
    ASSERT_EQ(result.size(), static_cast<size_t>(4));
    ASSERT_EQ(result["yt.d.cpu.user_time{thread=Profiler}"], 2.0);
    ASSERT_EQ(result["yt.d.cpu.user_time{}"], 2.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{thread=Profiler}"], 25.0);
    ASSERT_EQ(result["yt.d.cpu.system_time{}"], 25.0);

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, CustomProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d");

    auto c0 = r.Counter("/simple_sharded");
    c0.Increment();

    auto c1 = r.Counter("/simple_sharded");
    c1.Increment();

    auto g0 = r.WithExcludedTag("node_shard", "0").Gauge("/excluded_tag");
    g0.Update(10);

    auto g1 = r.WithExcludedTag("node_shard", "1").Gauge("/excluded_tag");
    g1.Update(20);

    auto c2 = r
        .WithRequiredTag("bundle", "sys")
        .WithTag("table_path", "//sys/operations")
        .Counter("/request_count");
    c2.Increment();

    auto c3 = r
        .WithTag("medium", "ssd")
        .WithTag("disk", "ssd0", -1)
        .Counter("/iops");
    c3.Increment();

    auto result = CollectSensors(impl);
    ASSERT_EQ(result.Counters["yt.d.simple_sharded{}"], 2u);

    ASSERT_EQ(result.Gauges["yt.d.excluded_tag{}"], 30.0);
    ASSERT_EQ(result.Gauges.size(), static_cast<size_t>(1));

    ASSERT_EQ(result.Counters["yt.d.request_count{bundle=sys}"], 1u);
    ASSERT_EQ(result.Counters["yt.d.request_count{bundle=sys;table_path=//sys/operations}"], 1u);
    ASSERT_TRUE(result.Counters.find("yt.d.request_count{}") == result.Counters.end());
    ASSERT_TRUE(result.Counters.find("yt.d.request_count{table_path=//sys/operations}") == result.Counters.end());

    CollectSensors(impl, 2);
    CollectSensors(impl, 3);
}

TEST(TSolomonRegistry, DisableProjections)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d");

    auto p0 = New<TDebugProducer>();
    r.WithProjectionsDisabled().AddProducer("/bigb", p0);

    {
        TWithTagGuard guard(&p0->Buffer, TTag{"mode", "sum"});
        p0->Buffer.AddGauge("", 10);
    }

    {
        TWithTagGuard guard(&p0->Buffer, TTag{"mode", "percentile"});
        {
            TWithTagGuard guard(&p0->Buffer, TTag{"p", "50"});
            p0->Buffer.AddCounter("", 20);
        }
        {
            TWithTagGuard guard(&p0->Buffer, TTag{"p", "99"});
            p0->Buffer.AddCounter("", 1);
        }
    }

    auto result = CollectSensors(impl);
    ASSERT_EQ(1u, result.Gauges.size());
    ASSERT_EQ(10.0, result.Gauges["yt.d.bigb{mode=sum}"]);

    ASSERT_EQ(2u, result.Counters.size());
    ASSERT_EQ(20, result.Counters["yt.d.bigb{mode=percentile;p=50}"]);
    ASSERT_EQ(1, result.Counters["yt.d.bigb{mode=percentile;p=99}"]);
}

TEST(TSolomonRegistry, DisableRenaming)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d", "");

    auto p0 = New<TDebugProducer>();
    r.WithRenameDisabled().AddProducer("/bigb", p0);
    p0->Buffer.AddGauge("/gauge", 10);
    p0->Buffer.AddCounter("/counter", 5);


    auto result = CollectSensors(impl);
    ASSERT_EQ(1u, result.Gauges.size());
    EXPECT_EQ(10.0, result.Gauges["/d/bigb/gauge{}"]);

    ASSERT_EQ(1u, result.Counters.size());
    EXPECT_EQ(5, result.Counters["/d/bigb/counter{}"]);
}

DECLARE_REFCOUNTED_STRUCT(TCounterProducer)

struct TCounterProducer
    : public ISensorProducer
{
    int i = 0;

    virtual void CollectSensors(ISensorWriter* writer)
    {
        writer->AddCounter("/counter", ++i);
    }
};

DEFINE_REFCOUNTED_TYPE(TCounterProducer)

TEST(TSolomonRegistry, CounterProducer)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d");

    auto p0 = New<TCounterProducer>();
    r.WithProjectionsDisabled().AddProducer("", p0);

    auto result = CollectSensors(impl).Counters;
    ASSERT_EQ(1, result["yt.d.counter{}"]);

    result = CollectSensors(impl).Counters;
    ASSERT_EQ(2, result["yt.d.counter{}"]);

    result = CollectSensors(impl).Counters;
    ASSERT_EQ(3, result["yt.d.counter{}"]);
}

DECLARE_REFCOUNTED_STRUCT(TBadProducer)

struct TBadProducer
    : public ISensorProducer
{
    virtual void CollectSensors(ISensorWriter*)
    {
        THROW_ERROR_EXCEPTION("Unavailable");
    }
};

DEFINE_REFCOUNTED_TYPE(TBadProducer)

TEST(TSolomonRegistry, Exceptions)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d");

    auto producer = New<TBadProducer>();
    r.AddProducer("/p", producer);
    r.AddFuncCounter("/c", producer, [] () -> i64 {
        THROW_ERROR_EXCEPTION("Unavailable");
    });
    r.AddFuncGauge("/g", producer, [] () -> double {
        THROW_ERROR_EXCEPTION("Unavailable");
    });

    impl->ProcessRegistrations();
    impl->Collect();
}

TEST(TSolomonRegistry, CounterTagsBug)
{
    auto impl = New<TSolomonRegistry>();
    impl->SetWindowSize(12);
    TProfiler r(impl, "/d");

    auto r1 = r.WithTag("client", "1");

    TTagList tags;
    tags.emplace_back("cluster", "hahn");

    auto c = r1.WithTags(TTagSet{tags}).Counter("/foo");
    c.Increment();

    impl->ProcessRegistrations();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NProfiling
