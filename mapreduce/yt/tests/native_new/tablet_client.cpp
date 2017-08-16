#include "lib.h"

#include <mapreduce/yt/http/error.h>

#include <library/unittest/registar.h>

#include <util/generic/algorithm.h>

using namespace NYT;
using namespace NYT::NTesting;

void WaitForTableState(const IClientPtr& client, const TYPath& table, TStringBuf state);

class TTabletFixture
{
public:
    TTabletFixture()
    {
        Client_ = CreateTestClient();
        WaitForTabletCell();
    }

    IClientPtr Client()
    {
        return Client_;
    }

private:
    void WaitForTabletCell()
    {
        const TInstant deadline = TInstant::Now() + TDuration::Seconds(30);
        while (TInstant::Now() < deadline) {
            auto tabletCellList = Client()->List(
                "//sys/tablet_cells",
                TListOptions().AttributeFilter(
                    TAttributeFilter().AddAttribute("health")));
            if (!tabletCellList.empty()) {
                bool good = true;
                for (const auto& tabletCell : tabletCellList) {
                    const auto health = tabletCell.GetAttributes()["health"].AsString();
                    if (health != "good") {
                        good = false;
                        break;
                    }
                }
                if (good) {
                    return;
                }
            }
            Sleep(TDuration::MilliSeconds(100));
        }
        ythrow yexception() << "WaitForTabletCell timeout";
    }

    IClientPtr Client_;
};

void CreateTestTable(const IClientPtr& client, const TYPath& table)
{
    client->Create(
        table,
        NT_TABLE,
        TCreateOptions()
        .Attributes(
            TNode()
            ("dynamic", true)
            ("schema", TNode()
             .Add(TNode()("name", "key")("type", "int64")("sort_order", "ascending"))
             .Add(TNode()("name", "value")("type", "string")))));
}

void CreateTestMulticolumnTable(const IClientPtr& client, const TYPath& table)
{
    client->Create(
        table,
        NT_TABLE,
        TCreateOptions()
        .Attributes(
            TNode()
            ("dynamic", true)
            ("schema", TNode()
             .Add(TNode()("name", "key")("type", "int64")("sort_order", "ascending"))
             .Add(TNode()("name", "value1")("type", "string"))
             .Add(TNode()("name", "value2")("type", "string")))));
}

void CreateTestAggregatingTable(const IClientPtr& client, const TYPath& table)
{
    client->Create(
        table,
        NT_TABLE,
        TCreateOptions()
        .Attributes(
            TNode()
            ("dynamic", true)
            ("schema", TNode()
             .Add(TNode()("name", "key")("type", "string")("sort_order", "ascending"))
             .Add(TNode()("name", "value")("type", "int64")("aggregate", "sum")))));
}

void WaitForTableState(const IClientPtr& client, const TYPath& table, TStringBuf state)
{
    yvector<TYPath> tabletStatePathList;
    const TInstant deadline = TInstant::Now() + TDuration::Seconds(30);
    while (TInstant::Now() < deadline) {
        auto tabletList = client->Get(TStringBuilder() << table << "/@tablets");

        bool good = true;
        for (const auto& tablet : tabletList.AsList()) {
            if (tablet["state"].AsString() != state) {
                good = false;
                break;
            }
        }
        if (good) {
            return;
        }

        Sleep(TDuration::MilliSeconds(100));
    }
    ythrow yexception() << "WaitForTableState timeout";
}

SIMPLE_UNIT_TEST_SUITE(TabletClient) {
    SIMPLE_UNIT_TEST(TestMountUnmount)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-mount-unmount";
        CreateTestTable(client, tablePath);

        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        client->RemountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");

        client->MountTable(tablePath, TMountTableOptions().Freeze(true));
        WaitForTableState(client, tablePath, "frozen");

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }

    SIMPLE_UNIT_TEST(TestFreezeUnfreeze)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-freeze-unfreeze-1";
        CreateTestTable(client, tablePath);

        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        client->FreezeTable(tablePath);
        WaitForTableState(client, tablePath, "frozen");

        client->UnfreezeTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }

    SIMPLE_UNIT_TEST(TestReshard)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-reshard";
        CreateTestTable(client, tablePath);
        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        TNode::TList rows;
        for (int i = 0; i < 16; ++i) {
            rows.push_back(TNode()("key", i)("value", ToString(i)));
        }
        client->InsertRows(tablePath, rows);

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");

        yvector<TKey> pivotKeys;
        pivotKeys.push_back(TKey());
        pivotKeys.push_back(4);
        pivotKeys.push_back(8);
        pivotKeys.push_back(12);

        client->ReshardTable(tablePath, pivotKeys);

        const auto& tabletList = client->Get(TStringBuilder() << tablePath << "/@tablets");
        UNIT_ASSERT_VALUES_EQUAL(tabletList.AsList().size(), 4);

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }

    SIMPLE_UNIT_TEST(TestInsertLookupDelete)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-insert-lookup-delete";
        CreateTestTable(client, tablePath);
        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        TNode::TList rows = {
            TNode()("key", 1)("value", "one"),
            TNode()("key", 42)("value", "forty two"),
        };
        client->InsertRows(tablePath, rows);

        {
            auto result = client->LookupRows(tablePath, {TNode()("key", 42), TNode()("key", 1)});
            UNIT_ASSERT_VALUES_EQUAL(result, TNode::TList({rows[1], rows[0]}));
        }

        client->DeleteRows(tablePath, {TNode()("key", 42)});

        {
            auto result = client->LookupRows(tablePath, {TNode()("key", 42), TNode()("key", 1)});
            UNIT_ASSERT_VALUES_EQUAL(result, TNode::TList({rows[0]}));
        }

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }

    SIMPLE_UNIT_TEST(TestAtomicityNoneInsert)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-atomicity-insert";
        CreateTestTable(client, tablePath);
        client->Set(tablePath + "/@atomicity", "none");
        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        TNode::TList rows = {
            TNode()("key", 1)("value", "one"),
            TNode()("key", 42)("value", "forty two"),
        };
        UNIT_ASSERT_EXCEPTION(
            client->InsertRows(tablePath, rows),
            TErrorResponse);

        client->InsertRows(tablePath, rows, TInsertRowsOptions().Atomicity(EAtomicity::None));

        {
            auto result = client->LookupRows(tablePath, {TNode()("key", 42), TNode()("key", 1)});
            UNIT_ASSERT_VALUES_EQUAL(result, TNode::TList({rows[1], rows[0]}));
        }

        UNIT_ASSERT_EXCEPTION(
            client->DeleteRows(tablePath, {TNode()("key", 42)}),
            TErrorResponse);

        client->DeleteRows(tablePath, {TNode()("key", 42)},
            TDeleteRowsOptions().Atomicity(EAtomicity::None));

        {
            auto result = client->LookupRows(tablePath, {TNode()("key", 42), TNode()("key", 1)});
            UNIT_ASSERT_VALUES_EQUAL(result, TNode::TList({rows[0]}));
        }

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }

    SIMPLE_UNIT_TEST(TestTimeoutType)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-timeout-type";
        CreateTestTable(client, tablePath);
        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        TNode::TList rows = {
            TNode()("key", 1)("value", "one"),
            TNode()("key", 42)("value", "forty two"),
        };
        client->InsertRows(tablePath, rows);

        {
            auto result = client->LookupRows(tablePath,
                {TNode()("key", 42), TNode()("key", 1)},
                NYT::TLookupRowsOptions().Timeout(TDuration::Seconds(1)));
            UNIT_ASSERT_VALUES_EQUAL(result, TNode::TList({rows[1], rows[0]}));
        }

        {
            auto result = client->SelectRows("* from [//testing/test-timeout-type]", NYT::TSelectRowsOptions().Timeout(TDuration::Seconds(1)));
            //Sort(result.begin(), result.end(), [] (const TNode& lhs, const TNode& rhs) {
                    //return lhs["key"].AsInt64() < rhs["key"].AsInt64();
                //});
            UNIT_ASSERT_VALUES_EQUAL(result, rows);
        }

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }

    SIMPLE_UNIT_TEST(TestUpdateInsert)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-update-insert";
        CreateTestMulticolumnTable(client, tablePath);
        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        client->InsertRows(tablePath, {TNode()("key", 1)("value1", "one")("value2", "odin")});

        {
            auto result = client->LookupRows(tablePath, {TNode()("key", 1)});
            UNIT_ASSERT_VALUES_EQUAL(result, std::vector<TNode>{TNode()("key", 1)("value1", "one")("value2", "odin")});
        }

        client->InsertRows(tablePath, {TNode()("key", 1)("value1", "two")}, TInsertRowsOptions().Update(true));
        {
            auto result = client->LookupRows(tablePath, {TNode()("key", 1)});
            UNIT_ASSERT_VALUES_EQUAL(result, std::vector<TNode>{TNode()("key", 1)("value1", "two")("value2", "odin")});
        }

        client->InsertRows(tablePath, {TNode()("key", 1)("value2", "dva")});
        {
            auto result = client->LookupRows(tablePath, {TNode()("key", 1)});
            UNIT_ASSERT_VALUES_EQUAL(result, std::vector<TNode>{TNode()("key", 1)("value1", TNode::CreateEntity())("value2", "dva")});
        }

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }

    SIMPLE_UNIT_TEST(TestAggregateInsert)
    {
        TTabletFixture fixture;
        auto client = fixture.Client();
        const TString tablePath = "//testing/test-aggregate-insert";
        CreateTestAggregatingTable(client, tablePath);
        client->MountTable(tablePath);
        WaitForTableState(client, tablePath, "mounted");

        client->InsertRows(tablePath, {TNode()("key", "one")("value", 5)});

        {
            auto result = client->LookupRows(tablePath, {TNode()("key", "one")});
            UNIT_ASSERT_VALUES_EQUAL(result, std::vector<TNode>{TNode()("key", "one")("value", 5)});
        }

        client->InsertRows(tablePath, {TNode()("key", "one")("value", 5)}, TInsertRowsOptions().Aggregate(true));
        {
            auto result = client->LookupRows(tablePath, {TNode()("key", "one")});
            UNIT_ASSERT_VALUES_EQUAL(result, std::vector<TNode>{TNode()("key", "one")("value", 10)});
        }

        client->InsertRows(tablePath, {TNode()("key", "one")("value", 5)});
        {
            auto result = client->LookupRows(tablePath, {TNode()("key", "one")});
            UNIT_ASSERT_VALUES_EQUAL(result, std::vector<TNode>{TNode()("key", "one")("value", 5)});
        }

        client->UnmountTable(tablePath);
        WaitForTableState(client, tablePath, "unmounted");
    }
}
