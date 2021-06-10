#include "helpers.h"

#include <yt/yt/client/table_client/versioned_reader.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

void CheckEqual(const TUnversionedValue& expected, const TUnversionedValue& actual)
{
    SCOPED_TRACE(Format("Expected: %v; Actual: %v", expected, actual));
    EXPECT_TRUE(AreRowValuesIdentical(expected, actual));
}

void CheckEqual(const TVersionedValue& expected, const TVersionedValue& actual)
{
    SCOPED_TRACE(Format("Expected: %v; Actual: %v", expected, actual));
    EXPECT_TRUE(AreRowValuesIdentical(expected, actual));
}

void ExpectSchemafulRowsEqual(TUnversionedRow expected, TUnversionedRow actual)
{
    SCOPED_TRACE(Format("Expected: %v; Actual: %v", expected, actual));

    ASSERT_EQ(static_cast<bool>(expected), static_cast<bool>(actual));
    if (!expected || !actual) {
        return;
    }
    ASSERT_EQ(expected.GetCount(), actual.GetCount());

    for (int valueIndex = 0; valueIndex < static_cast<int>(expected.GetCount()); ++valueIndex) {
        SCOPED_TRACE(Format("Value index %v", valueIndex));
        CheckEqual(expected[valueIndex], actual[valueIndex]);
    }
}

void ExpectSchemalessRowsEqual(TUnversionedRow expected, TUnversionedRow actual, int keyColumnCount)
{
    SCOPED_TRACE(Format("Expected: %v; Actual: %v", expected, actual));

    ASSERT_EQ(static_cast<bool>(expected), static_cast<bool>(actual));
    if (!expected || !actual) {
        return;
    }
    ASSERT_EQ(expected.GetCount(), actual.GetCount());

    for (int valueIndex = 0; valueIndex < keyColumnCount; ++valueIndex) {
        SCOPED_TRACE(Format("Value index %v", valueIndex));
        CheckEqual(expected[valueIndex], actual[valueIndex]);
    }

    for (int valueIndex = keyColumnCount; valueIndex < static_cast<int>(expected.GetCount()); ++valueIndex) {
        SCOPED_TRACE(Format("Value index %v", valueIndex));

        // Find value with the same id. Since this in schemaless read, value positions can be different.
        bool found = false;
        for (int index = keyColumnCount; index < static_cast<int>(expected.GetCount()); ++index) {
            if (expected[valueIndex].Id == actual[index].Id) {
                CheckEqual(expected[valueIndex], actual[index]);
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }
}

void ExpectSchemafulRowsEqual(TVersionedRow expected, TVersionedRow actual)
{
    SCOPED_TRACE(Format("Expected: %v; Actual: %v", expected, actual));

    ASSERT_EQ(static_cast<bool>(expected), static_cast<bool>(actual));
    if (!expected || !actual) {
        return;
    }

    ASSERT_EQ(expected.GetWriteTimestampCount(), actual.GetWriteTimestampCount());
    for (int i = 0; i < expected.GetWriteTimestampCount(); ++i) {
        SCOPED_TRACE(Format("Write Timestamp %v", i));
        EXPECT_EQ(expected.BeginWriteTimestamps()[i], actual.BeginWriteTimestamps()[i]);
    }

    ASSERT_EQ(expected.GetDeleteTimestampCount(), actual.GetDeleteTimestampCount());
    for (int i = 0; i < expected.GetDeleteTimestampCount(); ++i) {
        SCOPED_TRACE(Format("Delete Timestamp %v", i));
        EXPECT_EQ(expected.BeginDeleteTimestamps()[i], actual.BeginDeleteTimestamps()[i]);
    }

    ASSERT_EQ(expected.GetKeyCount(), actual.GetKeyCount());
    for (int index = 0; index < expected.GetKeyCount(); ++index) {
        SCOPED_TRACE(Format("Key index %v", index));
        CheckEqual(expected.BeginKeys()[index], actual.BeginKeys()[index]);
    }

    ASSERT_EQ(expected.GetValueCount(), actual.GetValueCount());
    for (int index = 0; index < expected.GetValueCount(); ++index) {
        SCOPED_TRACE(Format("Value index %v", index));
        CheckEqual(expected.BeginValues()[index], actual.BeginValues()[index]);
    }
}

void CheckResult(std::vector<TVersionedRow>* expected, IVersionedReaderPtr reader)
{
    expected->erase(
        std::remove_if(
            expected->begin(),
            expected->end(),
            [] (TVersionedRow row) {
                return !row;
            }),
        expected->end());

    auto it = expected->begin();
    std::vector<TVersionedRow> actual;
    actual.reserve(1000);

    while (auto batch = reader->Read()) {
        if (batch->IsEmpty()) {
            EXPECT_TRUE(reader->GetReadyEvent().Get().IsOK());
            continue;
        }

        auto range = batch->MaterializeRows();
        std::vector<TVersionedRow> actual(range.begin(), range.end());

        actual.erase(
            std::remove_if(
                actual.begin(),
                actual.end(),
                [] (TVersionedRow row) {
                    return !row;
                }),
            actual.end());

        std::vector<TVersionedRow> ex(it, it + actual.size());

        CheckSchemafulResult(ex, actual);
        it += actual.size();
    }

    EXPECT_TRUE(it == expected->end());
}

std::vector<std::pair<ui32, ui32>> GetTimestampIndexRanges(
    TRange<TVersionedRow> rows,
    TTimestamp timestamp)
{
    std::vector<std::pair<ui32, ui32>> indexRanges;
    for (auto row : rows) {
        // Find delete timestamp.
        NTableClient::TTimestamp deleteTimestamp = NTableClient::NullTimestamp;
        for (auto deleteIt = row.BeginDeleteTimestamps(); deleteIt != row.EndDeleteTimestamps(); ++deleteIt) {
            if (*deleteIt <= timestamp) {
                deleteTimestamp = std::max(*deleteIt, deleteTimestamp);
            }
        }

        int lowerTimestampIndex = 0;
        while (lowerTimestampIndex < row.GetWriteTimestampCount() &&
               row.BeginWriteTimestamps()[lowerTimestampIndex] > timestamp)
        {
            ++lowerTimestampIndex;
        }

        int upperTimestampIndex = lowerTimestampIndex;
        while (upperTimestampIndex < row.GetWriteTimestampCount() &&
               row.BeginWriteTimestamps()[upperTimestampIndex] > deleteTimestamp)
        {
            ++upperTimestampIndex;
        }

        indexRanges.push_back(std::make_pair(lowerTimestampIndex, upperTimestampIndex));
    }
    return indexRanges;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
