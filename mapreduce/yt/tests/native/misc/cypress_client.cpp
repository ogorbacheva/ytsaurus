#include <mapreduce/yt/tests/yt_unittest_lib/yt_unittest_lib.h>

#include <mapreduce/yt/tests/native/proto_lib/row.pb.h>

#include <mapreduce/yt/interface/errors.h>
#include <mapreduce/yt/interface/serialize.h>

#include <mapreduce/yt/http/abortable_http_response.h>

#include <library/unittest/registar.h>

#include <library/threading/future/async.h>

#include <library/digest/md5/md5.h>

#include <util/generic/guid.h>
#include <util/string/cast.h>
#include <util/thread/pool.h>

using namespace NYT;
using namespace NYT::NTesting;

static TNode::TListType SortedStrings(TNode::TListType input) {
    std::sort(input.begin(), input.end(), [] (const TNode& lhs, const TNode& rhs) {
        return lhs.AsString() < rhs.AsString();
    });
    return input;
}

template <typename T>
T MakeCopy(const T& t) {
    return t;
}

Y_UNIT_TEST_SUITE(CypressClient) {
    Y_UNIT_TEST(TestCreateAllTypes)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();

        const ENodeType nodeTypeList[] = {
            NT_STRING,
            NT_INT64,
            NT_UINT64,
            NT_DOUBLE,
            NT_BOOLEAN,
            NT_MAP,
            NT_LIST,
            NT_FILE,
            NT_TABLE,
            NT_DOCUMENT,
        };

        for (const auto nodeType : nodeTypeList) {
            auto nodeTypeStr = ::ToString(nodeType);
            const TString nodePath = workingDir + "/" + nodeTypeStr;
            const TString nodeTypePath = nodePath + "/@type";
            const TString nodeIdPath = nodePath + "/@id";

            auto nodeId = client->Create(nodePath, nodeType);
            UNIT_ASSERT_VALUES_EQUAL(client->Get(nodeTypePath), nodeTypeStr);
            UNIT_ASSERT_VALUES_EQUAL(client->Get(nodeIdPath), GetGuidAsString(nodeId));
        }
    }

    Y_UNIT_TEST(TestCreate)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        auto tx = client->StartTransaction();

        client->Create(workingDir + "/map_node", NT_MAP);
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/map_node"), true);

        tx->Create(workingDir + "/tx_map_node", NT_MAP);
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/tx_map_node"), false);
        UNIT_ASSERT_VALUES_EQUAL(tx->Exists(workingDir + "/tx_map_node"), true);

        UNIT_ASSERT_EXCEPTION(
            client->Create(workingDir + "/recursive_not_set_dir/node", NT_TABLE),
            TErrorResponse);
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/recursive_not_set_dir"), false);

        client->Create(workingDir + "/recursive_set_dir/node", NT_TABLE, TCreateOptions().Recursive(true));
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/recursive_set_dir"), true);

        client->Create(workingDir + "/existing_table", NT_TABLE);
        UNIT_ASSERT_EXCEPTION(
            client->Create(workingDir + "/existing_table", NT_TABLE),
            TErrorResponse);
        client->Create(workingDir + "/existing_table", NT_TABLE, TCreateOptions().IgnoreExisting(true));
        UNIT_ASSERT_EXCEPTION(
            client->Create(workingDir + "/existing_table", NT_MAP, TCreateOptions().IgnoreExisting(true)),
            TErrorResponse);

        client->Create(workingDir + "/node_with_attributes", NT_TABLE, TCreateOptions().Attributes(TNode()("attr_name", "attr_value")));
        UNIT_ASSERT_VALUES_EQUAL(
            client->Get(workingDir + "/node_with_attributes/@attr_name"),
            TNode("attr_value"));

        {
            auto initialNodeId = client->Create(workingDir + "/existing_table_for_force", NT_TABLE);

            auto nonForceNodeId = client->Create(workingDir + "/existing_table_for_force", NT_TABLE, TCreateOptions().IgnoreExisting(true));
            UNIT_ASSERT_VALUES_EQUAL(initialNodeId, nonForceNodeId);
            auto forceNodeId = client->Create(workingDir + "/existing_table_for_force", NT_TABLE, TCreateOptions().Force(true));
            UNIT_ASSERT(forceNodeId != initialNodeId);
        }
    }

    Y_UNIT_TEST(TestCreateProtobufTable)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();

        auto id = client->CreateTable<NYT::NTesting::TUrlRow>(
            workingDir + "/table",
            {"Host"});

        UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/table/@id").AsString(), GetGuidAsString(id));

        auto schemaNode = client->Get(workingDir + "/table/@schema");
        TTableSchema schema;
        Deserialize(schema, schemaNode);

        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_.size(), 3);

        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_[0].Name_, "Host");
        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_[0].Type_, EValueType::VT_STRING);
        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_[0].SortOrder_, ESortOrder::SO_ASCENDING);

        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_[1].Name_, "Path");
        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_[1].Type_, EValueType::VT_STRING);
        UNIT_ASSERT(schema.Columns_[1].SortOrder_.Empty());

        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_[2].Name_, "HttpCode");
        UNIT_ASSERT_VALUES_EQUAL(schema.Columns_[2].Type_, EValueType::VT_INT32);
        UNIT_ASSERT(schema.Columns_[2].SortOrder_.Empty());
    }

    Y_UNIT_TEST(TestCreateHugeAttribute)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        const TString hugeAttribute(1024 * 1024, 'a');
        client->Create(workingDir + "/table", NT_TABLE,
            TCreateOptions().Attributes(TNode()("huge_attribute", hugeAttribute)));
        UNIT_ASSERT_EQUAL(client->Get(workingDir + "/table/@huge_attribute").AsString(), hugeAttribute);
    }

    Y_UNIT_TEST(TestRemove)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        auto tx = client->StartTransaction();

        client->Create(workingDir + "/table", NT_TABLE);
        client->Remove(workingDir + "/table");
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/table"), false);

        tx->Create(workingDir + "/tx_table", NT_TABLE);
        tx->Remove(workingDir + "/tx_table");
        UNIT_ASSERT_VALUES_EQUAL(tx->Exists(workingDir + "/tx_table"), false);

        client->Create(workingDir + "/map_node/table_node", NT_TABLE, TCreateOptions().Recursive(true));

        UNIT_ASSERT_EXCEPTION(
            client->Remove(workingDir + "/map_node"),
            TErrorResponse);
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/map_node/table_node"), true);
        client->Remove(workingDir + "/map_node", TRemoveOptions().Recursive(true));
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/map_node"), false);

        UNIT_ASSERT_EXCEPTION(
            client->Remove(workingDir + "/missing_node"),
            TErrorResponse);
        client->Remove(workingDir + "/missing_node", TRemoveOptions().Force(true));
    }

    Y_UNIT_TEST(TestSetGet)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        const TNode nodeList[] = {
            TNode("foobar"),
            TNode(ui64(42)),
            TNode(i64(-100500)),
            TNode(3.14),
            TNode(true),
            TNode(false),
            TNode().Add("gg").Add("lol").Add(100500),
            TNode()("key1", "value1")("key2", "value2"),
        };

        for (const auto& node : nodeList) {
            client->Remove(workingDir + "/node", TRemoveOptions().Recursive(true).Force(true));
            client->Set(workingDir + "/node", node);
            UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/node"), node);
        }

        {
            TNode node("Recursive");
            UNIT_ASSERT_EXCEPTION(client->Set(workingDir + "/node/with/some/path", node), yexception);
            client->Set(workingDir + "/node/with/some/path", node, TSetOptions().Recursive(true));
            UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/node/with/some/path"), node);
        }
        {
            auto node = TNode()("key", "value");
            client->Remove(workingDir + "/node", TRemoveOptions().Force(true).Recursive(true));
            client->Create(workingDir + "/node", ENodeType::NT_MAP);
            // TODO(levysotsky): Uncomment when set will be forbidden by default.
            // UNIT_ASSERT_EXCEPTION(client->Set(workingDir + "/node", node), yexception);
            client->Set(workingDir + "/node", node, TSetOptions().Force(true));
            UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/node"), node);
        }

        auto tx = client->StartTransaction();
        tx->Set(workingDir + "/tx_node", TNode(10050));
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/tx_node"), false);
        UNIT_ASSERT_VALUES_EQUAL(tx->Get(workingDir + "/tx_node"), TNode(10050));

        client->Create(workingDir + "/node_with_attr", NT_TABLE);
        client->Set(workingDir + "/node_with_attr/@attr_name", TNode("attr_value"));

        auto nodeWithAttr = client->Get(workingDir + "/node_with_attr",
            TGetOptions().AttributeFilter(TAttributeFilter().AddAttribute("attr_name")));

        UNIT_ASSERT_VALUES_EQUAL(nodeWithAttr.GetAttributes().AsMap().at("attr_name"), TNode("attr_value"));
    }

    Y_UNIT_TEST(TestList)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        auto tx = client->StartTransaction();
        client->Set(workingDir + "/foo", 5);
        client->Set(workingDir + "/bar", "bar");
        client->Set(workingDir + "/bar", "bar");
        client->Set(workingDir + "/bar/@attr_name", "attr_value");
        tx->Set(workingDir + "/tx_qux", "gg");

        auto res = client->List(workingDir + "");

        UNIT_ASSERT_VALUES_EQUAL(
            SortedStrings(res),
            TNode::TListType({"bar", "foo"}));

        auto txRes = tx->List(workingDir + "");
        UNIT_ASSERT_VALUES_EQUAL(
            SortedStrings(txRes),
            TNode::TListType({"bar", "foo", "tx_qux"}));

        auto maxSizeRes = client->List(workingDir + "", TListOptions().MaxSize(1));
        UNIT_ASSERT_VALUES_EQUAL(maxSizeRes.size(), 1);
        UNIT_ASSERT(THashSet<TString>({"foo", "bar"}).contains(maxSizeRes[0].AsString()));

        auto attrFilterRes = client->List(workingDir + "",
            TListOptions().AttributeFilter(TAttributeFilter().AddAttribute("attr_name")));
        attrFilterRes = SortedStrings(attrFilterRes);
        auto barNode = TNode("bar");
        barNode.Attributes()("attr_name", "attr_value");
        UNIT_ASSERT_VALUES_EQUAL(
            attrFilterRes,
            TNode::TListType({barNode, "foo"}));
    }

    // YT-10354
    Y_UNIT_TEST(TestListEmptyAttributeFilter)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        auto tx = client->StartTransaction();
        client->Set(workingDir + "/foo", 5);
        client->Set(workingDir + "/bar", "bar");

        NYT::TAttributeFilter filter;
        auto res = client->List(workingDir, NYT::TListOptions().AttributeFilter(std::move(filter)));

        UNIT_ASSERT_VALUES_EQUAL(
            SortedStrings(res),
            TNode::TListType({"bar", "foo"}));
    }

    Y_UNIT_TEST(TestCopy)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();

        client->Set(workingDir + "/simple", "simple value");
        client->Copy(workingDir + "/simple", workingDir + "/copy_simple");
        UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/copy_simple"), client->Get(workingDir + "/simple"));
    }

    Y_UNIT_TEST(TestMove)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();

        client->Set(workingDir + "/simple", "simple value");
        auto oldValue = client->Get(workingDir + "/simple");
        client->Move(workingDir + "/simple", workingDir + "/moved_simple");
        UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/moved_simple"), oldValue);
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/simple"), false);
    }

    Y_UNIT_TEST(TestCopy_PreserveExpirationTime)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();

        const TString expirationTime = "2042-02-15T18:45:19.591902Z";
        for (TString path : {workingDir + "/table_default", workingDir + "/table_false", workingDir + "/table_true"}) {
            client->Create(path, NT_TABLE);
            client->Set(path + "/@expiration_time", expirationTime);
        }

        client->Copy(workingDir + "/table_default", workingDir + "/copy_table_default");
        client->Copy(workingDir + "/table_true", workingDir + "/copy_table_true", TCopyOptions().PreserveExpirationTime(true));
        client->Copy(workingDir + "/table_false", workingDir + "/copy_table_false", TCopyOptions().PreserveExpirationTime(false));

        UNIT_ASSERT_EXCEPTION(client->Get(workingDir + "/copy_table_default/@expiration_time"), yexception);
        UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/copy_table_true/@expiration_time"), expirationTime);
        UNIT_ASSERT_EXCEPTION(client->Get(workingDir + "/copy_table_false/@expiration_time"), yexception);
    }

    Y_UNIT_TEST(TestMove_PreserveExpirationTime)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();

        const TString expirationTime = "2042-02-15T18:45:19.591902Z";
        for (TString path : {workingDir + "/table_default", workingDir + "/table_false", workingDir + "/table_true"}) {
            client->Create(path, NT_TABLE);
            client->Set(path + "/@expiration_time", expirationTime);
        }

        client->Move(workingDir + "/table_default", workingDir + "/moved_table_default");
        client->Move(workingDir + "/table_true", workingDir + "/moved_table_true", TMoveOptions().PreserveExpirationTime(true));
        client->Move(workingDir + "/table_false", workingDir + "/moved_table_false", TMoveOptions().PreserveExpirationTime(false));

        // TODO(levysotsky) Uncomment when default behaviour is stable
        // UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/moved_table_default/@expiration_time"), TNode(expirationTime));
        UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/moved_table_true/@expiration_time"), TNode(expirationTime));
        UNIT_ASSERT_EXCEPTION(client->Get(workingDir + "/moved_table_false/@expiration_time"), yexception);
    }

    Y_UNIT_TEST(TestLink)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();

        client->Create(workingDir + "/table", NT_TABLE);
        client->Create(workingDir + "/table2", NT_TABLE);
        client->Link(workingDir + "/table", workingDir + "/table_link");

        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/table"), true);
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/table_link"), true);
        UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/table_link&/@target_path"), workingDir + "/table");

        UNIT_ASSERT_EXCEPTION(client->Link(workingDir + "/table2", workingDir + "/table_link"), yexception);

        client->Link(workingDir + "/table2", workingDir + "/table_link", NYT::TLinkOptions().Force(true));
        UNIT_ASSERT_VALUES_EQUAL(client->Exists(workingDir + "/table2"), true);
        UNIT_ASSERT_VALUES_EQUAL(client->Get(workingDir + "/table_link&/@target_path"), workingDir + "/table2");
    }

    Y_UNIT_TEST(TestConcatenate)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        {
            auto writer = client->CreateFileWriter(workingDir + "/file1");
            *writer << "foo";
            writer->Finish();
        }
        {
            auto writer = client->CreateFileWriter(workingDir + "/file2");
            *writer << "bar";
            writer->Finish();
        }
        client->Create(workingDir + "/concat", NT_FILE);
        TVector<TYPath> nodes{workingDir + "/file1", workingDir + "/file2"};
        client->Concatenate(nodes, workingDir + "/concat");
        {
            auto reader = client->CreateFileReader(workingDir + "/concat");
            UNIT_ASSERT_VALUES_EQUAL(reader->ReadAll(), "foobar");
        }
        client->Concatenate(nodes, workingDir + "/concat", TConcatenateOptions().Append(true));
        {
            auto reader = client->CreateFileReader(workingDir + "/concat");
            UNIT_ASSERT_VALUES_EQUAL(reader->ReadAll(), "foobarfoobar");
        }
    }

    Y_UNIT_TEST(TestTxConcatenate)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        {
            auto writer = client->CreateFileWriter(workingDir + "/file1");
            *writer << "foo";
            writer->Finish();
        }
        {
            auto writer = client->CreateFileWriter(workingDir + "/file2");
            *writer << "bar";
            writer->Finish();
        }
        auto tx = client->StartTransaction();
        tx->Create(workingDir + "/concat", NT_FILE);
        tx->Concatenate({workingDir + "/file1", workingDir + "/file2"}, workingDir + "/concat");
        {
            auto reader = tx->CreateFileReader(workingDir + "/concat");
            UNIT_ASSERT_VALUES_EQUAL(reader->ReadAll(), "foobar");
        }
        UNIT_ASSERT(!client->Exists(workingDir + "/concat"));
    }

    Y_UNIT_TEST(TestRetries)
    {
        TConfig::Get()->UseAbortableResponse = true;
        TConfig::Get()->RetryCount = 4;

        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        client->Create(workingDir + "/table", NT_MAP);
        {
            auto outage = TAbortableHttpResponse::StartOutage("/set");
            try {
                client->Set(workingDir + "/table/@my_attr", 42);
                UNIT_FAIL("Set() must have been thrown");
            } catch (const TAbortedForTestPurpose&) {
                // It's OK
            }
        }
        {
            auto outage = TAbortableHttpResponse::StartOutage("/set", TConfig::Get()->RetryCount - 1);
            UNIT_ASSERT_NO_EXCEPTION(client->Set(workingDir + "/table/@my_attr", -43));
        }
    }

    Y_UNIT_TEST(TestGetColumnarStatistics)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        auto tx = client->StartTransaction();
        {
            auto writer = tx->CreateTableWriter<TNode>(workingDir + "/table");
            writer->AddRow(TNode()("foo", 1)("bar", "baz"));
            writer->AddRow(TNode()("foo", 2)("bar", "qux"));
            writer->Finish();
        }

        auto statisticsList = tx->GetTableColumnarStatistics({ TRichYPath(workingDir + "/table").Columns({"bar", "foo"}) });
        const auto& statistics = statisticsList.front();

        UNIT_ASSERT_VALUES_EQUAL(statistics.ColumnDataWeight.size(), 2);
        UNIT_ASSERT(statistics.ColumnDataWeight.at("foo") > 0);
        UNIT_ASSERT(statistics.ColumnDataWeight.at("bar") > 0);

        UNIT_ASSERT_EXCEPTION(
            client->GetTableColumnarStatistics({ TRichYPath(workingDir + "/table").Columns({"bar", "foo"}) }),
            TErrorResponse);
    }

    Y_UNIT_TEST(TestConcurrency) {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        client->Set(workingDir + "/foo", 54);

        auto threadPool = CreateThreadPool(20);

        const auto writer = [&] {
            for (int i = 0; i != 500; ++i) {
                client->Set(workingDir + "/foo", 42);
            }
        };

        const auto reader = [&] {
            for (int i = 0; i != 500; ++i) {
                client->Get(workingDir + "/foo");
            }
        };

        TVector<NThreading::TFuture<void>> results;
        for (int i = 0; i != 10; ++i) {
            results.emplace_back(NThreading::Async(MakeCopy(writer), *threadPool));
        };
        for (int i = 0; i != 10; ++i) {
            results.emplace_back(NThreading::Async(MakeCopy(reader), *threadPool));
        };

        for (auto& f : results) {
            f.Wait();
        }
    }

    Y_UNIT_TEST(FileCache)
    {
        TTestFixture fixture;
        auto client = fixture.GetClient();
        auto workingDir = fixture.GetWorkingDir();
        TYPath cachePath = "//tmp/yt_wrapper/file_storage/new_cache";
        client->Create(cachePath, ENodeType::NT_MAP, TCreateOptions().IgnoreExisting(true));

        {
            TString content = "Hello world!";
            {
                auto writer = client->CreateFileWriter(workingDir + "/file", TFileWriterOptions().ComputeMD5(true));
                *writer << content;
                writer->Finish();
            }

            auto md5 = MD5::Calc(content);
            auto pathInCache = client->PutFileToCache(workingDir + "/file", md5, cachePath);

            auto maybePath = client->GetFileFromCache(md5, cachePath);
            UNIT_ASSERT(maybePath.Defined());
            UNIT_ASSERT_VALUES_EQUAL(content, client->CreateFileReader(*maybePath)->ReadAll());
        }

        {
            auto tx = client->StartTransaction();

            TString content = "Hello world again!";
            {
                auto writer = tx->CreateFileWriter(workingDir + "/file2", TFileWriterOptions().ComputeMD5(true));
                *writer << content;
                writer->Finish();
            }

            auto md5 = MD5::Calc(content);
            auto pathInCache = tx->PutFileToCache(workingDir + "/file2", md5, cachePath);

            auto maybePath = tx->GetFileFromCache(md5, cachePath);
            UNIT_ASSERT(maybePath.Defined());
            UNIT_ASSERT_VALUES_EQUAL(content, tx->CreateFileReader(*maybePath)->ReadAll());

            maybePath = client->GetFileFromCache(md5, cachePath);
            UNIT_ASSERT(!maybePath.Defined());

            tx->Commit();

            maybePath = client->GetFileFromCache(md5, cachePath);
            UNIT_ASSERT(maybePath.Defined());
            UNIT_ASSERT_VALUES_EQUAL(content, client->CreateFileReader(*maybePath)->ReadAll());
        }
    }
}
