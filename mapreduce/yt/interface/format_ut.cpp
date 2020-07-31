#include "common.h"
#include "errors.h"
#include "format.h"
#include "common_ut.h"

#include <mapreduce/yt/interface/protobuf_table_schema_ut.pb.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace NYT;

static TNode GetColumns(const TFormat& format, int tableIndex = 0)
{
    return format.Config.GetAttributes()["tables"][tableIndex]["columns"];
}

Y_UNIT_TEST_SUITE(ProtobufFormat)
{
    Y_UNIT_TEST(TIntegral)
    {
        const auto format = TFormat::Protobuf<NTesting::TIntegral>();
        auto columns = GetColumns(format);

        struct TColumn
        {
            TString Name;
            TString ProtoType;
            int FieldNumber;
        };

        auto expected = TVector<TColumn>{
            {"DoubleField", "double", 1},
            {"FloatField", "float", 2},
            {"Int32Field", "int32", 3},
            {"Int64Field", "int64", 4},
            {"Uint32Field", "uint32", 5},
            {"Uint64Field", "uint64", 6},
            {"Sint32Field", "sint32", 7},
            {"Sint64Field", "sint64", 8},
            {"Fixed32Field", "fixed32", 9},
            {"Fixed64Field", "fixed64", 10},
            {"Sfixed32Field", "sfixed32", 11},
            {"Sfixed64Field", "sfixed64", 12},
            {"BoolField", "bool", 13},
            {"EnumField", "enum_string", 14},
        };

        UNIT_ASSERT_VALUES_EQUAL(columns.Size(), expected.size());
        for (int i = 0; i < static_cast<int>(columns.Size()); ++i) {
            UNIT_ASSERT_VALUES_EQUAL(columns[i]["name"], expected[i].Name);
            UNIT_ASSERT_VALUES_EQUAL(columns[i]["proto_type"], expected[i].ProtoType);
            UNIT_ASSERT_VALUES_EQUAL(columns[i]["field_number"], expected[i].FieldNumber);
        }
    }

    Y_UNIT_TEST(TRowFieldSerializationOption)
    {
        const auto format = TFormat::Protobuf<NTesting::TRowFieldSerializationOption>();
        auto columns = GetColumns(format);

        UNIT_ASSERT_VALUES_EQUAL(columns[0]["name"], "UrlRow_1");
        UNIT_ASSERT_VALUES_EQUAL(columns[0]["proto_type"], "structured_message");
        UNIT_ASSERT_VALUES_EQUAL(columns[0]["field_number"], 1);
        const auto& fields = columns[0]["fields"];
        UNIT_ASSERT_VALUES_EQUAL(fields[0]["name"], "Host");
        UNIT_ASSERT_VALUES_EQUAL(fields[0]["proto_type"], "string");
        UNIT_ASSERT_VALUES_EQUAL(fields[0]["field_number"], 1);

        UNIT_ASSERT_VALUES_EQUAL(fields[1]["name"], "Path");
        UNIT_ASSERT_VALUES_EQUAL(fields[1]["proto_type"], "string");
        UNIT_ASSERT_VALUES_EQUAL(fields[1]["field_number"], 2);

        UNIT_ASSERT_VALUES_EQUAL(fields[2]["name"], "HttpCode");
        UNIT_ASSERT_VALUES_EQUAL(fields[2]["proto_type"], "sint32");
        UNIT_ASSERT_VALUES_EQUAL(fields[2]["field_number"], 3);

        UNIT_ASSERT_VALUES_EQUAL(columns[1]["name"], "UrlRow_2");
        UNIT_ASSERT_VALUES_EQUAL(columns[1]["proto_type"], "message");
        UNIT_ASSERT_VALUES_EQUAL(columns[1]["field_number"], 2);
    }

    Y_UNIT_TEST(Packed)
    {
        const auto format = TFormat::Protobuf<NTesting::TPacked>();
        auto column = GetColumns(format)[0];

        UNIT_ASSERT_VALUES_EQUAL(column["name"], "PackedListInt64");
        UNIT_ASSERT_VALUES_EQUAL(column["proto_type"], "int64");
        UNIT_ASSERT_VALUES_EQUAL(column["field_number"], 1);
        UNIT_ASSERT_VALUES_EQUAL(column["packed"], true);
        UNIT_ASSERT_VALUES_EQUAL(column["repeated"], true);
    }

    Y_UNIT_TEST(Cyclic)
    {
        UNIT_ASSERT_EXCEPTION(TFormat::Protobuf<NTesting::TCyclic>(), TApiUsageError);
        UNIT_ASSERT_EXCEPTION(TFormat::Protobuf<NTesting::TCyclic::TA>(), TApiUsageError);
        UNIT_ASSERT_EXCEPTION(TFormat::Protobuf<NTesting::TCyclic::TB>(), TApiUsageError);
        UNIT_ASSERT_EXCEPTION(TFormat::Protobuf<NTesting::TCyclic::TC>(), TApiUsageError);
        UNIT_ASSERT_EXCEPTION(TFormat::Protobuf<NTesting::TCyclic::TD>(), TApiUsageError);

        const auto format = TFormat::Protobuf<NTesting::TCyclic::TE>();
        auto column = GetColumns(format)[0];
        UNIT_ASSERT_VALUES_EQUAL(column["name"], "d");
        UNIT_ASSERT_VALUES_EQUAL(column["proto_type"], "message");
        UNIT_ASSERT_VALUES_EQUAL(column["field_number"], 1);
    }

    Y_UNIT_TEST(Map)
    {
        const auto format = TFormat::Protobuf<NTesting::TWithMap>();
        auto columns = GetColumns(format);

        UNIT_ASSERT_VALUES_EQUAL(columns.Size(), 5);
        {
            const auto& column = columns[0];
            UNIT_ASSERT_VALUES_EQUAL(column["name"], "MapDefault");
            UNIT_ASSERT_VALUES_EQUAL(column["proto_type"], "structured_message");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"].Size(), 2);
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][0]["proto_type"], "int64");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][1]["proto_type"], "message");
        }
        {
            const auto& column = columns[1];
            UNIT_ASSERT_VALUES_EQUAL(column["name"], "MapListOfStructsLegacy");
            UNIT_ASSERT_VALUES_EQUAL(column["proto_type"], "structured_message");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"].Size(), 2);
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][0]["proto_type"], "int64");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][1]["proto_type"], "message");
        }
        {
            const auto& column = columns[2];
            UNIT_ASSERT_VALUES_EQUAL(column["name"], "MapListOfStructs");
            UNIT_ASSERT_VALUES_EQUAL(column["proto_type"], "structured_message");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"].Size(), 2);
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][0]["proto_type"], "int64");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][1]["proto_type"], "structured_message");
        }
        {
            const auto& column = columns[3];
            UNIT_ASSERT_VALUES_EQUAL(column["name"], "MapOptionalDict");
            UNIT_ASSERT_VALUES_EQUAL(column["proto_type"], "structured_message");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"].Size(), 2);
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][0]["proto_type"], "int64");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][1]["proto_type"], "structured_message");
        }
        {
            const auto& column = columns[4];
            UNIT_ASSERT_VALUES_EQUAL(column["name"], "MapDict");
            UNIT_ASSERT_VALUES_EQUAL(column["proto_type"], "structured_message");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"].Size(), 2);
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][0]["proto_type"], "int64");
            UNIT_ASSERT_VALUES_EQUAL(column["fields"][1]["proto_type"], "structured_message");
        }
    }
}
