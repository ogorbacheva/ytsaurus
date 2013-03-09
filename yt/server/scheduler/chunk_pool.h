#pragma once

#include "private.h"
#include "progress_counter.h"

#include <ytlib/misc/small_vector.h>

#include <ytlib/chunk_client/public.h>

#include <ytlib/table_client/table_reader.pb.h>

#include <server/chunk_server/public.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

struct TChunkStripeStatistics
{
    int ChunkCount;
    i64 DataSize;
    i64 RowCount;

    TChunkStripeStatistics()
        : ChunkCount(0)
        , DataSize(0)
        , RowCount(0)
    { }
};

TChunkStripeStatistics operator + (
    const TChunkStripeStatistics& lhs,
    const TChunkStripeStatistics& rhs);

TChunkStripeStatistics& operator += (
    TChunkStripeStatistics& lhs,
    const TChunkStripeStatistics& rhs);

//! Adds up input statistics and returns a single-item vector with the sum.
std::vector<TChunkStripeStatistics> AggregateStatistics(
    const std::vector<TChunkStripeStatistics>& statistics);

////////////////////////////////////////////////////////////////////////////////

struct TChunkStripe
    : public TIntrinsicRefCounted
{
    TChunkStripe();
    explicit TChunkStripe(NTableClient::TRefCountedInputChunkPtr inputChunk);
    explicit TChunkStripe(const TChunkStripe& other);

    TChunkStripeStatistics GetStatistics() const;

    TSmallVector<NTableClient::TRefCountedInputChunkPtr, 1> Chunks;
};

////////////////////////////////////////////////////////////////////////////////

struct TChunkStripeList
    : public TIntrinsicRefCounted
{
    TChunkStripeList();

    std::vector<TChunkStripeStatistics> GetStatistics() const;

    std::vector<TChunkStripePtr> Stripes;

    TNullable<int> PartitionTag;

    //! If True then TotalDataSize and TotalRowCount are approximate (and are hopefully upper bounds).
    bool IsApproximate;

    i64 TotalDataSize;
    i64 TotalRowCount;

    int TotalChunkCount;
    int LocalChunkCount;
    int NonLocalChunkCount;

};

////////////////////////////////////////////////////////////////////////////////

struct IChunkPoolInput
{
    virtual ~IChunkPoolInput()
    { }

    typedef int TCookie;
    static const TCookie NullCookie = -1;

    virtual TCookie Add(TChunkStripePtr stripe) = 0;

    virtual void Suspend(TCookie cookie) = 0;
    virtual void Resume(TCookie cookie, TChunkStripePtr stripe) = 0;
    virtual void Finish() = 0;

};

////////////////////////////////////////////////////////////////////////////////

struct IChunkPoolOutput
{
    virtual ~IChunkPoolOutput()
    { }

    typedef int TCookie;
    static const TCookie NullCookie = -1;

    virtual i64 GetTotalDataSize() const = 0;
    virtual i64 GetRunningDataSize() const = 0;
    virtual i64 GetCompletedDataSize() const = 0;
    virtual i64 GetPendingDataSize() const = 0;

    virtual i64 GetTotalRowCount() const = 0;

    virtual bool IsCompleted() const = 0;

    virtual int GetTotalJobCount() const = 0;
    virtual int GetPendingJobCount() const = 0;

    // Approximate average stripe list statistics to estimate memory usage.
    virtual std::vector<TChunkStripeStatistics> GetApproximateStripeStatistics() const = 0;

    virtual i64 GetLocality(const Stroka& address) const = 0;

    virtual TCookie Extract(const Stroka& address) = 0;

    virtual TChunkStripeListPtr GetStripeList(TCookie cookie) = 0;

    virtual void Completed(TCookie cookie) = 0;
    virtual void Failed(TCookie cookie) = 0;
    virtual void Aborted(TCookie cookie) = 0;
    virtual void Lost(TCookie cookie) = 0;

};

////////////////////////////////////////////////////////////////////////////////

struct IChunkPool
    : public virtual IChunkPoolInput
    , public virtual IChunkPoolOutput
{ };

TAutoPtr<IChunkPool> CreateAtomicChunkPool(
    NChunkClient::TNodeDirectoryPtr nodeDirectory);

TAutoPtr<IChunkPool> CreateUnorderedChunkPool(
    NChunkClient::TNodeDirectoryPtr nodeDirectory,
    int jobCount);

////////////////////////////////////////////////////////////////////////////////

struct IShuffleChunkPool
{
    virtual ~IShuffleChunkPool()
    { }

    virtual IChunkPoolInput* GetInput() = 0;
    virtual IChunkPoolOutput* GetOutput(int partitionIndex) = 0;
};

TAutoPtr<IShuffleChunkPool> CreateShuffleChunkPool(
    NChunkClient::TNodeDirectoryPtr nodeDirectory,
    int partitionCount,
    i64 dataSizeThreshold);

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

