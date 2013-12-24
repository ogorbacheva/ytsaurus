#include "stdafx.h"
#include "memory_store_ut.h"

#include <yt/core/profiling/scoped_timer.h>

#include <util/random/random.h>

namespace NYT {
namespace NTabletNode {
namespace {

using namespace NChunkClient;
using namespace NTransactionClient;
using namespace NProfiling;

////////////////////////////////////////////////////////////////////////////////

class TMemoryStorePerfTest
    : public TMemoryStoreTestBase
{
public:
    TMemoryStorePerfTest()
    {
        auto config = New<TTabletManagerConfig>();
        DynamicStore = New<TDynamicMemoryStore>(config, Tablet.get());
    }

    void RunDynamic(
        int iterationCount,
        int writePercentage)
    {
        Cerr << "Iterations: " << iterationCount << ", "
             << "WritePercentage: " << writePercentage
             << Endl;

        auto executeRead = [&] () {
            TUnversionedOwningRowBuilder builder;
            builder.AddValue(MakeUnversionedIntegerValue(RandomNumber<ui64>(1000000000), 0));

            auto key = builder.Finish();

            auto scanner = DynamicStore->CreateScanner();
            scanner->Find(key, LastCommittedTimestamp);
        };

        auto executeWrite = [&] () {
            auto transaction = StartTransaction();

            TUnversionedOwningRowBuilder builder;
            builder.AddValue(MakeUnversionedIntegerValue(RandomNumber<ui64>(1000000000), 0));
            builder.AddValue(MakeUnversionedIntegerValue(123, 1));
            builder.AddValue(MakeUnversionedDoubleValue(3.1415, 2));
            builder.AddValue(MakeUnversionedStringValue("hello from YT", 3));
            auto row = builder.Finish();

            auto dynamicRow = DynamicStore->WriteRow(
                NameTable,
                transaction.get(),
                row,
                false);

            PrepareTransaction(transaction.get());
            DynamicStore->PrepareRow(dynamicRow);

            CommitTransaction(transaction.get());
            DynamicStore->CommitRow(dynamicRow);
        };

        Cerr << "Warming up..." << Endl;

        for (int iteration = 0; iteration < iterationCount; ++iteration) {
            executeWrite();
        }

        Cerr << "Testing..." << Endl;

        TScopedTimer timer;

        for (int iteration = 0; iteration < iterationCount; ++iteration) {
            if (RandomNumber<unsigned>(100) < writePercentage) {
                executeWrite();
            } else {
                executeRead();
            }
        }

        auto elapsed = timer.GetElapsed();
        Cerr << "Elapsed: " << elapsed.MilliSeconds() << "ms, "
             << "RPS: " << (int) iterationCount / elapsed.SecondsFloat() << Endl;
    }

private:
    TDynamicMemoryStorePtr DynamicStore;

};

///////////////////////////////////////////////////////////////////////////////

TEST_F(TMemoryStorePerfTest, DynamicWrite)
{
    RunDynamic(
        1000000,
        100);
}

TEST_F(TMemoryStorePerfTest, DynamicRead)
{
    RunDynamic(
        1000000,
        0);
}

TEST_F(TMemoryStorePerfTest, DynamicReadWrite)
{
    RunDynamic(
        1000000,
        50);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NTabletNode
} // namespace NYT
