#include <yt/core/test_framework/framework.h>

#include <yt/server/controller_agent/input_chunk_mapping.h>
#include <yt/server/controller_agent/helpers.h>
#include <yt/server/controller_agent/operation_controller.h>

#include <yt/ytlib/table_client/row_buffer.h>

#include <yt/core/misc/blob_output.h>

#include <random>

namespace NYT {
namespace NControllerAgent {
namespace {

using namespace NChunkClient;
using namespace NChunkPools;
using namespace NTableClient;

using NControllerAgent::TCompletedJobSummary;

using namespace ::testing;

////////////////////////////////////////////////////////////////////////////////

class TInputChunkMappingTest
    : public Test
{
protected:
    TInputChunkMappingPtr ChunkMapping_;

    TRowBufferPtr RowBuffer_ = New<TRowBuffer>();

    EChunkMappingMode Mode_;

    void InitChunkMapping(EChunkMappingMode mode)
    {
        Mode_ = mode;
        ChunkMapping_ = New<TInputChunkMapping>(mode);
    }

    // In this test we will only deal with integral rows as
    // all the logic inside sorted chunk pool does not depend on
    // actual type of values in keys.
    // TODO(max42): extract to common helper base.
    TKey BuildRow(std::vector<i64> values)
    {
        auto row = RowBuffer_->AllocateUnversioned(values.size());
        for (int index = 0; index < values.size(); ++index) {
            row[index] = MakeUnversionedInt64Value(values[index], index);
        }
        return row;
    }

    // TODO(max42): extract to common helper base.
    TInputChunkPtr CreateChunk(
        const TKey& minBoundaryKey = TKey(),
        const TKey& maxBoundaryKey = TKey(),
        i64 rowCount = 1000,
        i64 size = 1_KB)
    {
        auto inputChunk = New<TInputChunk>();
        inputChunk->ChunkId() = TChunkId::Create();
        inputChunk->SetCompressedDataSize(size);
        inputChunk->SetUncompressedDataSize(size);
        inputChunk->SetTotalDataWeight(size);
        inputChunk->BoundaryKeys() = std::make_unique<TOwningBoundaryKeys>(TOwningBoundaryKeys {
            TOwningKey(minBoundaryKey),
            TOwningKey(maxBoundaryKey)
        });
        inputChunk->SetTotalRowCount(rowCount);
        return inputChunk;
    }

    TChunkStripePtr CreateStripe(const std::vector<TInputChunkPtr>& chunks)
    {
        std::vector<TInputDataSlicePtr> dataSlices;
        for (const auto& chunk : chunks) {
            auto dataSlice = CreateUnversionedInputDataSlice(CreateInputChunkSlice(chunk));
            InferLimitsFromBoundaryKeys(dataSlice, RowBuffer_);
            dataSlices.emplace_back(std::move(dataSlice));
        }
        auto stripe = New<TChunkStripe>();
        std::move(dataSlices.begin(), dataSlices.end(), std::back_inserter(stripe->DataSlices));
        return stripe;
    }

    TInputChunkPtr CopyChunk(const TInputChunkPtr& chunk)
    {
        TInputChunkPtr chunkCopy = New<TInputChunk>();
        chunkCopy->ChunkId() = chunk->ChunkId();
        chunkCopy->SetCompressedDataSize(chunk->GetCompressedDataSize());
        chunkCopy->BoundaryKeys() = std::make_unique<TOwningBoundaryKeys>(*chunk->BoundaryKeys());
        chunkCopy->SetTotalRowCount(chunk->GetRowCount());
        return chunkCopy;
    }

    std::vector<TInputChunkPtr> ToChunks(const TChunkStripePtr& stripe)
    {
        std::vector<TInputChunkPtr> chunks;
        for (const auto& dataSlice : stripe->DataSlices) {
            chunks.emplace_back(dataSlice->GetSingleUnversionedChunkOrThrow());
        }
        return chunks;
    }

    std::vector<TChunkId> ToChunkIds(const std::vector<TInputChunkPtr>& chunks)
    {
        std::vector<TChunkId> result;
        for (const auto& chunk : chunks) {
            result.emplace_back(chunk->ChunkId());
        }
        return result;
    }

    bool Same(const std::vector<TInputChunkPtr>& lhs, const std::vector<TInputChunkPtr>& rhs)
    {
        auto lhsChunkIds = ToChunkIds(lhs);
        auto rhsChunkIds = ToChunkIds(rhs);
        if (Mode_ == EChunkMappingMode::Unordered) {
            sort(lhsChunkIds.begin(), lhsChunkIds.end());
            sort(rhsChunkIds.begin(), rhsChunkIds.end());
        }
        return lhsChunkIds == rhsChunkIds;
    }

    bool CheckMapping(TChunkStripePtr from, TChunkStripePtr to)
    {
        auto mappedFrom = ChunkMapping_->GetMappedStripe(from);
        return Same(ToChunks(mappedFrom), ToChunks(to));
    }
};

TEST_F(TInputChunkMappingTest, UnorderedSimple)
{
    InitChunkMapping(EChunkMappingMode::Unordered);

    auto chunkA = CreateChunk();
    auto chunkB = CreateChunk();
    auto chunkC = CreateChunk();
    auto chunkD = CreateChunk();
    auto chunkE = CreateChunk();
    auto chunkF = CreateChunk();

    auto stripeABC = CreateStripe({chunkA, chunkB, chunkC});
    auto stripeCBA = CreateStripe({chunkC, chunkB, chunkA});
    auto stripeACBD = CreateStripe({chunkA, chunkC, chunkB, chunkD});
    auto stripeEF = CreateStripe({chunkE, chunkF});
    auto stripeE = CreateStripe({chunkE});
    auto stripeF = CreateStripe({chunkF});
    auto stripeABCEF = CreateStripe({chunkA, chunkB, chunkC, chunkE, chunkF});
    auto stripeABCDEF = CreateStripe({chunkA, chunkB, chunkC, chunkD, chunkE, chunkF});
    auto stripeABCDF = CreateStripe({chunkA, chunkB, chunkC, chunkD, chunkF});
    auto stripeCBAE = CreateStripe({chunkC, chunkB, chunkA, chunkE});
    auto stripeCBAF = CreateStripe({chunkC, chunkB, chunkA, chunkF});
    auto stripeABCDE = CreateStripe({chunkA, chunkB, chunkC, chunkD, chunkE});

    ChunkMapping_->Add(42, stripeABC);
    EXPECT_TRUE(CheckMapping(stripeABC, stripeABC));
    // In unordered chunk mapping order does not matter (as one could expect).
    EXPECT_TRUE(CheckMapping(stripeCBA, stripeABC));
    ChunkMapping_->Add(23, stripeEF);
    EXPECT_TRUE(CheckMapping(stripeABC, stripeABC));
    EXPECT_TRUE(CheckMapping(stripeCBA, stripeABC));
    EXPECT_TRUE(CheckMapping(stripeEF, stripeEF));
    EXPECT_TRUE(CheckMapping(stripeABCEF, stripeABCEF));
    ChunkMapping_->OnStripeRegenerated(42, stripeACBD);
    EXPECT_TRUE(CheckMapping(stripeABC, stripeACBD));
    EXPECT_TRUE(CheckMapping(stripeCBA, stripeACBD));
    EXPECT_TRUE(CheckMapping(stripeEF, stripeEF));
    EXPECT_TRUE(CheckMapping(stripeABCEF, stripeABCDEF));
    ChunkMapping_->OnChunkDisappeared(chunkF);
    EXPECT_TRUE(CheckMapping(stripeEF, stripeE));
    EXPECT_TRUE(CheckMapping(stripeABCEF, stripeABCDE));
    ChunkMapping_->Reset(42, stripeCBA);
    EXPECT_TRUE(CheckMapping(stripeCBA, stripeCBA));
    EXPECT_TRUE(CheckMapping(stripeE, stripeE));
    ChunkMapping_->OnStripeRegenerated(23, stripeF);
    EXPECT_TRUE(CheckMapping(stripeE, stripeF));
}

TEST_F(TInputChunkMappingTest, SortedValidation)
{
    InitChunkMapping(EChunkMappingMode::Sorted);

    auto chunkA1 = CreateChunk(BuildRow({5}), BuildRow({15}), 1000 /* rowCount */);
    auto chunkA2 = CreateChunk(BuildRow({5}), BuildRow({15}), 1000 /* rowCount */); // Compatible.
    auto chunkA3 = CreateChunk(BuildRow({6}), BuildRow({15}), 1000 /* rowCount */); // Different min key.
    auto chunkA4 = CreateChunk(BuildRow({5}), BuildRow({16}), 1000 /* rowCount */); // Different max key.

    auto chunkB1 = CreateChunk(BuildRow({10}), BuildRow({20}), 2000 /* rowCount */);
    auto chunkB2 = CreateChunk(BuildRow({10}), BuildRow({20}), 2000 /* rowCount */); // Compatible.
    auto chunkB3 = CreateChunk(BuildRow({10}), BuildRow({20}), 2500 /* rowCount */); // Different row count.

    auto stripeA1B1 = CreateStripe({chunkA1, chunkB1});
    auto stripeA2B2 = CreateStripe({chunkA2, chunkB2});
    auto stripeA3B1 = CreateStripe({chunkA3, chunkB1});
    auto stripeA4B1 = CreateStripe({chunkA4, chunkB1});
    auto stripeA1B3 = CreateStripe({chunkA1, chunkB3});
    auto stripeB1A1 = CreateStripe({chunkB1, chunkA1});
    auto stripeA1B1B1 = CreateStripe({chunkA1, chunkB1, chunkB1});
    auto stripeB1 = CreateStripe({chunkB1});

    ChunkMapping_->Add(42, stripeA1B1);
    ChunkMapping_->OnStripeRegenerated(42, stripeA2B2);
    EXPECT_THROW(ChunkMapping_->OnStripeRegenerated(42, stripeA1B3), std::exception);
    EXPECT_THROW(ChunkMapping_->OnStripeRegenerated(42, stripeA3B1), std::exception);
    EXPECT_THROW(ChunkMapping_->OnStripeRegenerated(42, stripeA4B1), std::exception);
    EXPECT_THROW(ChunkMapping_->OnStripeRegenerated(42, stripeB1A1), std::exception);
    EXPECT_THROW(ChunkMapping_->OnStripeRegenerated(42, stripeA1B1B1), std::exception);
    EXPECT_THROW(ChunkMapping_->OnStripeRegenerated(42, stripeB1), std::exception);
    ChunkMapping_->Reset(42, stripeA1B3);
    EXPECT_THROW(ChunkMapping_->OnStripeRegenerated(42, stripeA2B2), std::exception);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NControllerAgent
} // namespace NYT
