#include "stdafx.h"
#include "framework.h"

#include <ytlib/object_client/helpers.h>

#include <ytlib/chunk_client/chunk_meta_extensions.h>
#include <ytlib/chunk_client/chunk_slice.h>
#include <ytlib/chunk_client/chunk_spec.h>
#include <ytlib/chunk_client/read_limit.h>
#include <ytlib/table_client/chunk_meta_extensions.h>
#include <ytlib/table_client/unversioned_row.h>

#include <core/yson/string.h>
#include <core/ytree/convert.h>
#include <core/ytree/node.h>

namespace NYT {

// Function is defined here due to ADL.
void PrintTo(NChunkClient::TChunkSlicePtr slice, ::std::ostream* os)
{
    *os << "chunk slice with " << slice->GetRowCount() << " rows";
}

namespace NChunkClient {
namespace {

using namespace NObjectClient;
using namespace NChunkClient::NProto;
using namespace NTableClient;
using namespace NTableClient::NProto;
using namespace NYTree;
using namespace NYson;

using NChunkClient::TReadLimit;

using testing::PrintToString;

//////////////////////////////////////////////////////////////////////////////

MATCHER_P(HasRowCount, rowCount, "has row count " + std::string(negation ? "not " : "") + PrintToString(rowCount))
{
    return arg->GetRowCount() == rowCount;
}

MATCHER_P(HasLowerLimit, lowerLimit, "has lower limit " + std::string(negation ? "not " : "") + lowerLimit)
{
    auto actualLimitAsNode = ConvertToNode(arg->LowerLimit());
    auto actualLimitAsText = ConvertToYsonString(actualLimitAsNode, EYsonFormat::Text).Data();

    auto expectedLimitAsNode = ConvertToNode(TYsonString(lowerLimit));

    *result_listener << "where HasLowerLimit(R\"_(" << actualLimitAsText.c_str() << ")_\")";

    return AreNodesEqual(actualLimitAsNode, expectedLimitAsNode);
}

MATCHER_P(HasUpperLimit, upperLimit, "has upper limit " + std::string(negation ? "not " : "") + upperLimit)
{
    auto actualLimitAsNode = ConvertToNode(arg->UpperLimit());
    auto actualLimitAsText = ConvertToYsonString(actualLimitAsNode, EYsonFormat::Text).Data();

    auto expectedLimitAsNode = ConvertToNode(TYsonString(upperLimit));

    auto x1 = ConvertToYsonString(actualLimitAsNode, EYsonFormat::Text).Data();
    auto x2 = ConvertToYsonString(expectedLimitAsNode, EYsonFormat::Text).Data();

    *result_listener << "where HasUpperLimit(R\"_(" << actualLimitAsText.c_str() << ")_\")";

    return AreNodesEqual(actualLimitAsNode, expectedLimitAsNode);
}

class TChunkSliceTest
    : public ::testing::Test
{
public:
    static const bool DoSliceByKeys = true;
    static const bool DoSliceByRows = false;

    TChunkSliceTest()
    {
        EmptyChunk_ = CreateChunkSpec(
            ETableChunkFormat::SchemalessHorizontal,
            1, // keyRepetitions
            0); // chunkRows

        OneKeyOldChunk_ = CreateChunkSpec(
            ETableChunkFormat::Old,
            300); // keyRepetitions
        OneKeyChunk_ = CreateChunkSpec(
            ETableChunkFormat::SchemalessHorizontal,
            300); // keyRepetitions

        TwoKeyOldChunk_ = CreateChunkSpec(
            ETableChunkFormat::Old,
            170); // keyRepetitions
        TwoKeyChunk_ = CreateChunkSpec(
            ETableChunkFormat::SchemalessHorizontal,
            170); // keyRepetitions

        OldChunkWithLimits_ = CreateChunkSpec(
            ETableChunkFormat::Old,
            2, // keyRepetitions
            300, // chunkRows
            "{lower_limit={key=[\"10010\"];row_index=100};upper_limit={}}");
        ChunkWithLimits_ = CreateChunkSpec(
            ETableChunkFormat::SchemalessHorizontal,
            2, // keyRepetitions
            300, // chunkRows
            "{lower_limit={key=[\"10010\"];row_index=100};upper_limit={}}");

        OldChunk2WithLimits_ = CreateChunkSpec(
            ETableChunkFormat::Old,
            2, // keyRepetitions
            300, // chunkRows
            "{lower_limit={};upper_limit={key=[\"10280\"];row_index=240}}",
            10150); // minKey
        Chunk2WithLimits_ = CreateChunkSpec(
            ETableChunkFormat::SchemalessHorizontal,
            2, // keyRepetitions
            300, // chunkRows
            "{lower_limit={};upper_limit={key=[\"10280\"];row_index=240}}",
            10150); // minKey
    }

protected:
    TRefCountedChunkSpecPtr EmptyChunk_;
    TRefCountedChunkSpecPtr OneKeyOldChunk_;
    TRefCountedChunkSpecPtr OneKeyChunk_;
    TRefCountedChunkSpecPtr TwoKeyOldChunk_;
    TRefCountedChunkSpecPtr TwoKeyChunk_;
    TRefCountedChunkSpecPtr OldChunkWithLimits_;
    TRefCountedChunkSpecPtr ChunkWithLimits_;
    TRefCountedChunkSpecPtr OldChunk2WithLimits_;
    TRefCountedChunkSpecPtr Chunk2WithLimits_;

    TGuid GenerateId(EObjectType type)
    {
        static i64 counter = 0;
        return MakeId(type, 0, counter++, 0);
    }

    TGuid GenerateChunkId()
    {
        return GenerateId(EObjectType::Chunk);
    }

    Stroka FormatKey(i64 key)
    {
        return Format("%05d", key);
    }

    TOwningKey KeyWithValue(i64 key)
    {
        TUnversionedOwningRowBuilder builder;
        builder.AddValue(MakeUnversionedStringValue(FormatKey(key)));
        return builder.FinishRow();
    }

    TRefCountedChunkSpecPtr CreateChunkSpec(
        ETableChunkFormat version,
        i64 keyRepetitions,         // the number of times that each key is repeated
        i64 chunkRows = 300,        // the number of rows in the chunk
        const Stroka& ysonRange = Stroka("{lower_limit={};upper_limit={}}"),
                                    // range for chunk, specified as yson text
        i64 minKey = 10000,         // minimal value of key
        i64 blockRows = 79,         // the number of rows per block
        i64 rowBytes = 13)          // the size of one block
    {
        TChunkMeta chunkMeta;
        chunkMeta.set_type(static_cast<int>(EChunkType::Table));
        chunkMeta.set_version(static_cast<int>(version));

        i64 maxKey = minKey + (chunkRows - 1) / keyRepetitions;
        i64 numBlocks = (chunkRows + blockRows - 1) / blockRows;
        if (version == ETableChunkFormat::Old) {
            TOldBoundaryKeysExt oldKeys;

            oldKeys.mutable_start()->add_parts();
            auto startKey = oldKeys.mutable_start()->mutable_parts(0);
            startKey->set_type(static_cast<int>(ELegacyKeyPartType::String));
            startKey->set_str_value(FormatKey(minKey));

            oldKeys.mutable_end()->add_parts();
            auto endKey = oldKeys.mutable_end()->mutable_parts(0);
            endKey->set_type(static_cast<int>(ELegacyKeyPartType::String));
            endKey->set_str_value(FormatKey(maxKey));

            SetProtoExtension<TOldBoundaryKeysExt>(chunkMeta.mutable_extensions(), oldKeys);

            TIndexExt indexExt;
            for (i64 i = 0; i < numBlocks-1; ++i) {
                indexExt.add_items();
                auto item = indexExt.mutable_items(i);

                i64 rowIndex = std::min((i + 1) * blockRows, chunkRows);
                item->set_row_index(rowIndex - 1);

                item->mutable_key()->add_parts();
                auto key = item->mutable_key()->mutable_parts(0);
                key->set_type(static_cast<int>(ELegacyKeyPartType::String));
                key->set_str_value(FormatKey(minKey + (rowIndex - 1) / keyRepetitions));
            }
            SetProtoExtension<TIndexExt>(chunkMeta.mutable_extensions(), indexExt);
        } else {
            TBoundaryKeysExt boundaryKeys;

            ToProto(boundaryKeys.mutable_min(), KeyWithValue(minKey));
            ToProto(boundaryKeys.mutable_max(), KeyWithValue(maxKey));

            SetProtoExtension<TBoundaryKeysExt>(chunkMeta.mutable_extensions(), boundaryKeys);

            TBlockMetaExt blockMetaExt;
            for (i64 i = 0; i < numBlocks; ++i) {
                blockMetaExt.add_blocks();
                auto block = blockMetaExt.mutable_blocks(i);

                i64 rowIndex = std::min((i + 1) * blockRows, chunkRows);
                i64 rowCount = rowIndex - i * blockRows;
                block->set_row_count(rowCount);
                block->set_uncompressed_size(rowCount * rowBytes);
                block->set_chunk_row_count(rowIndex);
                block->set_block_index(i);
                ToProto(block->mutable_last_key(), KeyWithValue(minKey + (rowIndex - 1) / keyRepetitions));
            }
            SetProtoExtension<TBlockMetaExt>(chunkMeta.mutable_extensions(), blockMetaExt);
        }

        TMiscExt miscExt;
        miscExt.set_row_count(chunkRows);
        miscExt.set_uncompressed_data_size(chunkRows * rowBytes);
        SetProtoExtension<TMiscExt>(chunkMeta.mutable_extensions(), miscExt);

        auto chunkSpec = New<TRefCountedChunkSpec>();
        ToProto(chunkSpec->mutable_chunk_id(), GenerateChunkId());
        auto range = ConvertTo<NChunkClient::TReadRange>(TYsonString(ysonRange));
        ToProto(chunkSpec->mutable_lower_limit(), range.LowerLimit());
        ToProto(chunkSpec->mutable_upper_limit(), range.UpperLimit());
        chunkSpec->mutable_chunk_meta()->Swap(&chunkMeta);

        return chunkSpec;
    }
};

TEST_F(TChunkSliceTest, OneKeyChunkSmallSlice)
{
    for (auto chunkSpec : {OneKeyOldChunk_, OneKeyChunk_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(1, keySlices.size());

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({key=["10000"]})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({key=["10000";<type=max>#]})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(300));

        auto rowSlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(7, rowSlices.size());

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10000"];"row_index"=0})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10000";<"type"="max">#];"row_index"=39})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(39));

        EXPECT_THAT(rowSlices[6], HasLowerLimit(R"_({"key"=["10000"];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[6], HasUpperLimit(R"_({"key"=["10000";<"type"="max">#];"row_index"=300})_"));
        EXPECT_THAT(rowSlices[6], HasRowCount(63));
    }
}

TEST_F(TChunkSliceTest, OneKeyChunkLargeSlice)
{
    for (auto chunkSpec : {OneKeyOldChunk_, OneKeyChunk_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(keySlices.size(), 1);

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({"key"=["10000"]})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({"key"=["10000";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(300));

        auto rowSlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(rowSlices.size(), 2);

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10000"];"row_index"=0})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10000";<"type"="max">#];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(237));

        EXPECT_THAT(rowSlices[1], HasLowerLimit(R"_({"key"=["10000"];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[1], HasUpperLimit(R"_({"key"=["10000";<"type"="max">#];"row_index"=300})_"));
        EXPECT_THAT(rowSlices[1], HasRowCount(63));
    }
}

TEST_F(TChunkSliceTest, TwoKeyChunkSmallSlice)
{
    for (auto chunkSpec : {TwoKeyOldChunk_, TwoKeyChunk_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(keySlices.size(), 2);

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({"key"=["10000"]})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({"key"=["10000";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(158));

        EXPECT_THAT(keySlices[1], HasLowerLimit(R"_({"key"=["10000";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[1], HasUpperLimit(R"_({"key"=["10001";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[1], HasRowCount(142));

        auto rowSlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(rowSlices.size(), 7);

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10000"];"row_index"=0})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10000";<"type"="max">#];"row_index"=39})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(39));

        EXPECT_THAT(rowSlices[6], HasLowerLimit(R"_({"key"=["10001"];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[6], HasUpperLimit(R"_({"key"=["10001";<"type"="max">#];"row_index"=300})_"));
        EXPECT_THAT(rowSlices[6], HasRowCount(63));
    }
}

TEST_F(TChunkSliceTest, TwoKeyChunkLargeSlice)
{
    for (auto chunkSpec : {TwoKeyOldChunk_, TwoKeyChunk_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(keySlices.size(), 1);

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({"key"=["10000"]})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({"key"=["10001";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(300));

        auto rowSlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(rowSlices.size(), 2);

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10000"];"row_index"=0})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10001";<"type"="max">#];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(237));

        EXPECT_THAT(rowSlices[1], HasLowerLimit(R"_({"key"=["10001"];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[1], HasUpperLimit(R"_({"key"=["10001";<"type"="max">#];"row_index"=300})_"));
        EXPECT_THAT(rowSlices[1], HasRowCount(63));
    }
}

TEST_F(TChunkSliceTest, ChunkWithLimitSmallSlice)
{
    for (auto chunkSpec : {OldChunkWithLimits_, ChunkWithLimits_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(keySlices.size(), 3);

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({"key"=["10039"];"row_index"=100})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({"key"=["10078";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(58));

        EXPECT_THAT(keySlices[2], HasLowerLimit(R"_({"key"=["10118";<"type"="max">#];"row_index"=100})_"));
        EXPECT_THAT(keySlices[2], HasUpperLimit(R"_({"key"=["10149";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[2], HasRowCount(63));

        auto rowSlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(rowSlices.size(), 4);

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10039"];"row_index"=100})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10078";<"type"="max">#];"row_index"=158})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(58));

        EXPECT_THAT(rowSlices[3], HasLowerLimit(R"_({"key"=["10118"];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[3], HasUpperLimit(R"_({"key"=["10149";<"type"="max">#];"row_index"=300})_"));
        EXPECT_THAT(rowSlices[3], HasRowCount(63));
    }
}

TEST_F(TChunkSliceTest, ChunkWithLimitLargeSlice)
{
    for (auto chunkSpec : {OldChunkWithLimits_, ChunkWithLimits_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(keySlices.size(), 1);

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({"key"=["10039"];"row_index"=100})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({"key"=["10149";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(200));

        auto rowSlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(rowSlices.size(), 1);

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10039"];"row_index"=100})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10149";<"type"="max">#];"row_index"=300})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(200));
    }
}

TEST_F(TChunkSliceTest, Chunk2WithLimitSmallSlice)
{
    for (auto chunkSpec : {OldChunk2WithLimits_, Chunk2WithLimits_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(keySlices.size(), 4);

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({"key"=["10150"]})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({"key"=["10189";<"type"="max">#];"row_index"=240})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(79));

        EXPECT_THAT(keySlices[3], HasLowerLimit(R"_({"key"=["10268";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[3], HasUpperLimit(R"_({"key"=["10280"];"row_index"=240})_"));
        EXPECT_THAT(keySlices[3], HasRowCount(3));

        auto rowSlices = SliceChunk(
            chunkSpec,
            500, // sliceDataSize
            1,   // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(rowSlices.size(), 7);

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10150"];"row_index"=0})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10189";<"type"="max">#];"row_index"=39})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(39));

        EXPECT_THAT(rowSlices[6], HasLowerLimit(R"_({"key"=["10268"];"row_index"=237})_"));
        EXPECT_THAT(rowSlices[6], HasUpperLimit(R"_({"key"=["10280"];"row_index"=240})_"));
        EXPECT_THAT(rowSlices[6], HasRowCount(3));
    }
}

TEST_F(TChunkSliceTest, Chunk2WithLimitLargeSlice)
{
    for (auto chunkSpec : {OldChunk2WithLimits_, Chunk2WithLimits_}) {
        auto keySlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByKeys);
        EXPECT_EQ(keySlices.size(), 2);

        EXPECT_THAT(keySlices[0], HasLowerLimit(R"_({"key"=["10150"]})_"));
        EXPECT_THAT(keySlices[0], HasUpperLimit(R"_({"key"=["10268";<"type"="max">#];"row_index"=240})_"));
        EXPECT_THAT(keySlices[0], HasRowCount(237));

        EXPECT_THAT(keySlices[1], HasLowerLimit(R"_({"key"=["10268";<"type"="max">#]})_"));
        EXPECT_THAT(keySlices[1], HasUpperLimit(R"_({"key"=["10280"];"row_index"=240})_"));
        EXPECT_THAT(keySlices[1], HasRowCount(3));

        auto rowSlices = SliceChunk(
            chunkSpec,
            2500, // sliceDataSize
            1,    // keyColumnCount
            DoSliceByRows);
        EXPECT_EQ(rowSlices.size(), 2);

        EXPECT_THAT(rowSlices[0], HasLowerLimit(R"_({"key"=["10150"]})_"));
        EXPECT_THAT(rowSlices[0], HasUpperLimit(R"_({"key"=["10268";<"type"="max">#];"row_index"=240})_"));
        EXPECT_THAT(rowSlices[0], HasRowCount(237));

        // XXX(sandello): Fixed copy-paste here.
        EXPECT_THAT(rowSlices[1], HasLowerLimit(R"_({"key"=["10268";<"type"="max">#]})_"));
        EXPECT_THAT(rowSlices[1], HasUpperLimit(R"_({"key"=["10280"];"row_index"=240})_"));
        EXPECT_THAT(rowSlices[1], HasRowCount(3));
    }
}

//////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NChunkServer
} // namespace NYT
