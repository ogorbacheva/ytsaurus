#include <mapreduce/yt/client/skiff.h>
#include <mapreduce/yt/interface/client.h>

#include <mapreduce/yt/node/node_io.h>

#include <library/yson/writer.h>

#include <util/system/env.h>
#include <util/string/type.h>

using namespace NYT;
using namespace NYT::NDetail;
using namespace NSkiff;

int main(int argc, const char** argv) {
    if (argc < 5) {
        Cout << "Usage: " << argv[0] << " <cypress-path> <num-rows> <local-path> <schema-local-path>" << Endl;
        return 1;
    }

    Initialize(argc, argv);
    const TString ytProxy = "freud";
    const TString cypressPath = argv[1];
    const auto numRows = FromString<i64>(argv[2]);
    const TString localPath = argv[3];
    const TString schemaLocalPath = argv[4];

    auto client = CreateClient("freud");
    auto schema = CreateVariant16Schema({CreateSkiffSchema(client->Get(cypressPath + "/@schema"))});
    {
        TOFStream schemaDump(schemaLocalPath);
        TYsonWriter writer(&schemaDump, YF_PRETTY);
        Serialize(schema, &writer);
    }
    {
        TOFStream dump(localPath);
        auto path = TRichYPath(cypressPath).AddRange(TReadRange().FromRowIndexes(0LL, numRows));
        auto reader = client->CreateRawReader(path, CreateSkiffFormat(schema));
        reader->ReadAll(dump);
    }

    return 0;
}
