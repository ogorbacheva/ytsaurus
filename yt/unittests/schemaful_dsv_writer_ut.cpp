#include "framework.h"

#include <yt/ytlib/formats/schemaful_dsv_writer.h>
#include <yt/ytlib/formats/format.h>

#include <yt/ytlib/table_client/name_table.h>

#include <yt/core/concurrency/async_stream.h>

#include <yt/core/misc/common.h>

#include <limits>

namespace NYT {
namespace NFormats {
namespace {

////////////////////////////////////////////////////////////////////////////////

using namespace NYTree;
using namespace NYson;
using namespace NConcurrency;
using namespace NTableClient;

class TSchemalessWriterForSchemafulDsvTest
    : public ::testing::Test
{
protected:
    TNameTablePtr NameTable_;
    int KeyAId_;
    int KeyBId_;
    int KeyCId_;
    int KeyDId_;
    TSchemafulDsvFormatConfigPtr Config_;

    ISchemalessFormatWriterPtr Writer_;

    TStringStream OutputStream_;

    TSchemalessWriterForSchemafulDsvTest() {
        NameTable_ = New<TNameTable>();
        KeyAId_ = NameTable_->RegisterName("column_a");
        KeyBId_ = NameTable_->RegisterName("column_b");
        KeyCId_ = NameTable_->RegisterName("column_c");
        KeyDId_ = NameTable_->RegisterName("column_d");
        Config_ = New<TSchemafulDsvFormatConfig>();
    }

    void CreateStandardWriter() {
        Writer_ = CreateSchemalessWriterForSchemafulDsv(
            *ConvertToAttributes(Config_),
            NameTable_, 
            CreateAsyncAdapter(static_cast<TOutputStream*>(&OutputStream_)),
            false, // enableContextSaving 
            false, // enableKeySwitch 
            0 /* keyColumnCount */);
    }
};

TEST_F(TSchemalessWriterForSchemafulDsvTest, Simple)
{
    Config_->Columns = {"column_b", "column_c", "column_a"};
    CreateStandardWriter();
    TUnversionedRowBuilder row1;
    row1.AddValue(MakeUnversionedStringValue("value_a", KeyAId_));
    row1.AddValue(MakeUnversionedInt64Value(-42, KeyBId_));
    row1.AddValue(MakeUnversionedBooleanValue(true, KeyCId_));
    row1.AddValue(MakeUnversionedStringValue("garbage", KeyDId_));

    TUnversionedRowBuilder row2;
    // The order is reversed.
    row2.AddValue(MakeUnversionedStringValue("value_c", KeyCId_));
    row2.AddValue(MakeUnversionedBooleanValue(false, KeyBId_));
    row2.AddValue(MakeUnversionedInt64Value(23, KeyAId_));

    std::vector<TUnversionedRow> rows = {row1.GetRow(), row2.GetRow()};

    EXPECT_EQ(true, Writer_->Write(rows));
    Writer_->Close()
        .Get()
        .ThrowOnError();
    
    Stroka expectedOutput =
        "-42\ttrue\tvalue_a\n"
        "false\tvalue_c\t23\n";
    EXPECT_EQ(expectedOutput, OutputStream_.Str()); 
}

// This test shows the actual behavior of writer. It is OK to change it in the future. :)
TEST_F(TSchemalessWriterForSchemafulDsvTest, TrickyDoubleRepresentations)
{
    Config_->Columns = {"column_a", "column_b", "column_c", "column_d"};
    CreateStandardWriter();
    TUnversionedRowBuilder row1;
    row1.AddValue(MakeUnversionedDoubleValue(1.234567890123456, KeyAId_));
    row1.AddValue(MakeUnversionedDoubleValue(42, KeyBId_));
    row1.AddValue(MakeUnversionedDoubleValue(1e300, KeyCId_));
    row1.AddValue(MakeUnversionedDoubleValue(-1e-300, KeyDId_));    

    std::vector<TUnversionedRow> rows = {row1.GetRow()};

    EXPECT_EQ(true, Writer_->Write(rows));
    Writer_->Close()
        .Get()
        .ThrowOnError();
    Stroka expectedOutput = "1.23457\t42\t1e+300\t-1e-300\n";
    EXPECT_EQ(expectedOutput, OutputStream_.Str());
}

TEST_F(TSchemalessWriterForSchemafulDsvTest, IntegralTypeRepresentations)
{
    Config_->Columns = {"column_a", "column_b", "column_c", "column_d"};
    CreateStandardWriter();
    
    TUnversionedRowBuilder row1;
    row1.AddValue(MakeUnversionedInt64Value(0LL, KeyAId_));
    row1.AddValue(MakeUnversionedInt64Value(-1LL, KeyBId_));
    row1.AddValue(MakeUnversionedInt64Value(1LL, KeyCId_));
    row1.AddValue(MakeUnversionedInt64Value(99LL, KeyDId_));
    
    TUnversionedRowBuilder row2;
    row2.AddValue(MakeUnversionedInt64Value(123LL, KeyAId_));
    row2.AddValue(MakeUnversionedInt64Value(-123LL, KeyBId_));
    row2.AddValue(MakeUnversionedInt64Value(1234LL, KeyCId_));
    row2.AddValue(MakeUnversionedInt64Value(-1234LL, KeyDId_));

    TUnversionedRowBuilder row3;
    row3.AddValue(MakeUnversionedUint64Value(0ULL, KeyAId_));
    row3.AddValue(MakeUnversionedUint64Value(98ULL, KeyBId_));
    row3.AddValue(MakeUnversionedUint64Value(987ULL, KeyCId_));
    row3.AddValue(MakeUnversionedUint64Value(9876ULL, KeyDId_));

    TUnversionedRowBuilder row4;
    row4.AddValue(MakeUnversionedInt64Value(std::numeric_limits<i64>::max(), KeyAId_));
    row4.AddValue(MakeUnversionedInt64Value(std::numeric_limits<i64>::min(), KeyBId_));
    row4.AddValue(MakeUnversionedInt64Value(std::numeric_limits<i64>::min() + 1LL, KeyCId_));
    row4.AddValue(MakeUnversionedUint64Value(std::numeric_limits<ui64>::max(), KeyDId_));

    std::vector<TUnversionedRow> rows = 
        {row1.GetRow(), row2.GetRow(), row3.GetRow(), row4.GetRow()};

    EXPECT_EQ(true, Writer_->Write(rows));
    Writer_->Close()
        .Get()
        .ThrowOnError();
    Stroka expectedOutput = 
        "0\t-1\t1\t99\n"
        "123\t-123\t1234\t-1234\n"
        "0\t98\t987\t9876\n"
        "9223372036854775807\t-9223372036854775808\t-9223372036854775807\t18446744073709551615\n";
    EXPECT_EQ(expectedOutput, OutputStream_.Str());
}

TEST_F(TSchemalessWriterForSchemafulDsvTest, MissingValueMode)
{
    Config_->Columns = {"column_a", "column_b", "column_c"};

    TUnversionedRowBuilder row1;
    row1.AddValue(MakeUnversionedStringValue("Value1A", KeyAId_));
    row1.AddValue(MakeUnversionedStringValue("Value1B", KeyBId_));
    row1.AddValue(MakeUnversionedStringValue("Value1C", KeyCId_));
    
    TUnversionedRowBuilder row2;
    row2.AddValue(MakeUnversionedStringValue("Value2A", KeyAId_));
    row2.AddValue(MakeUnversionedStringValue("Value2C", KeyCId_));

    TUnversionedRowBuilder row3;
    row3.AddValue(MakeUnversionedStringValue("Value3A", KeyAId_));
    row3.AddValue(MakeUnversionedStringValue("Value3B", KeyBId_));
    row3.AddValue(MakeUnversionedStringValue("Value3C", KeyCId_));
    
    std::vector<TUnversionedRow> rows = 
        {row1.GetRow(), row2.GetRow(), row3.GetRow()};
    
    {
        // By default missing_value_mode is EMissingSchemafulDsvValueMode::SkipRow. 
        CreateStandardWriter();
        EXPECT_EQ(true, Writer_->Write(rows));
        Writer_->Close()
            .Get()
            .ThrowOnError();
        Stroka expectedOutput = 
            "Value1A\tValue1B\tValue1C\n"
            "Value3A\tValue3B\tValue3C\n";
        EXPECT_EQ(expectedOutput, OutputStream_.Str());
        OutputStream_.Clear();
    }

    {
        Config_->MissingValueMode = EMissingSchemafulDsvValueMode::Fail;
        CreateStandardWriter();
        EXPECT_EQ(false, Writer_->Write(rows));
        EXPECT_THROW(Writer_->Close()
            .Get()
            .ThrowOnError(), std::exception);
        OutputStream_.Clear();
    } 

    {
        Config_->MissingValueMode = EMissingSchemafulDsvValueMode::PrintSentinel;
        Config_->MissingValueSentinel = "~";
        CreateStandardWriter();
        EXPECT_EQ(true, Writer_->Write(rows));
        Writer_->Close()
            .Get()
            .ThrowOnError();
        Stroka expectedOutput = 
            "Value1A\tValue1B\tValue1C\n"
            "Value2A\t~\tValue2C\n"
            "Value3A\tValue3B\tValue3C\n";
        EXPECT_EQ(expectedOutput, OutputStream_.Str());
        OutputStream_.Clear();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NFormats
} // namespace NYT
