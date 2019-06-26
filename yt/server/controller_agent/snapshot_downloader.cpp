#include "snapshot_downloader.h"
#include "bootstrap.h"
#include "config.h"

#include <yt/server/lib/scheduler/config.h>

#include <yt/client/api/file_reader.h>
#include <yt/ytlib/api/native/client.h>

#include <yt/ytlib/scheduler/helpers.h>

namespace NYT::NControllerAgent {

using namespace NApi;
using namespace NConcurrency;
using namespace NScheduler;

////////////////////////////////////////////////////////////////////////////////

TSnapshotDownloader::TSnapshotDownloader(
    TControllerAgentConfigPtr config,
    TBootstrap* bootstrap,
    TOperationId operationId)
    : Config_(config)
    , Bootstrap_(bootstrap)
    , OperationId_(operationId)
    , Logger(NLogging::TLogger(ControllerAgentLogger)
        .AddTag("OperationId: %v", operationId))
{
    YT_VERIFY(Config_);
    YT_VERIFY(Bootstrap_);
}

std::vector<TSharedRef> TSnapshotDownloader::Run()
{
    YT_LOG_INFO("Starting downloading snapshot");

    const auto& client = Bootstrap_->GetMasterClient();

    TFileReaderOptions options;
    options.Config = Config_->SnapshotReader;

    auto reader = WaitFor(client->CreateFileReader(GetSnapshotPath(OperationId_), options))
        .ValueOrThrow();

    YT_LOG_INFO("Snapshot reader opened");

    std::vector<TSharedRef> blocks;
    while (true) {
        auto blockOrError = WaitFor(reader->Read());
        auto block = blockOrError.ValueOrThrow();
        if (!block)
            break;
        blocks.push_back(block);
    }

    YT_LOG_INFO("Snapshot downloaded successfully");

    return blocks;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
