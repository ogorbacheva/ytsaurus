#include <yt/server/hydra/snapshot_quota_helpers.h>

#include <yt/core/test_framework/framework.h>

#include <limits>
#include <vector>

namespace NYT::NHydra {
namespace {

////////////////////////////////////////////////////////////////////////////////

TEST(TThresholdSnapshotId, EmptyVector)
{
    ASSERT_LE(GetSnapshotThresholdId({}, 1, 1), 0);
}

TEST(TThresholdSnapshotId, ShortVector)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 11}
    };
    ASSERT_LE(GetSnapshotThresholdId(snapshots, 3, std::nullopt), 0);
}

TEST(TThresholdSnapshotId, LongVector)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 11},
        {9, 15}
    };
    ASSERT_EQ(1, GetSnapshotThresholdId(snapshots, 2, std::nullopt));
}

TEST(TThresholdSnapshotId, SmallSize)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 3},
        {3, 7}
    };
    ASSERT_LE(GetSnapshotThresholdId(snapshots, std::nullopt, 30), 0);
}

TEST(TThresholdSnapshotId, BigSize)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 4},
        {3, 15},
        {4, 7}
    };
    ASSERT_EQ(2, GetSnapshotThresholdId(snapshots, std::nullopt, 25));
}

TEST(TThresholdSnapshotId, AccurateSize)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 11},
        {3, 13},
        {4, 14}
    };
    ASSERT_EQ(1, GetSnapshotThresholdId(snapshots, std::nullopt, 38));
}

TEST(TThresholdSnapshotId, CountStronger)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 11},
        {3, 13},
        {4, 14}
    };
    ASSERT_EQ(3, GetSnapshotThresholdId(snapshots, 1, 30));
}

TEST(TThresholdSnapshotId, SizeStronger)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 11},
        {3, 13},
        {4, 14}
    };
    ASSERT_EQ(3, GetSnapshotThresholdId(snapshots, 3, 15));
}

TEST(TThresholdSnapshotId, ZeroCount)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 11},
        {3, 13}
    };
    ASSERT_EQ(2, GetSnapshotThresholdId(snapshots, 0, std::nullopt));
}

TEST(TThresholdSnapshotId, ZeroSize)
{
    std::vector<TSnapshotInfo> snapshots{
        {1, 10},
        {2, 11},
        {3, 13}
    };
    ASSERT_EQ(2, GetSnapshotThresholdId(snapshots, std::nullopt, 0));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NHydra
