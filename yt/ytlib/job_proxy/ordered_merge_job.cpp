﻿#include "stdafx.h"
#include "ordered_merge_job.h"
#include "config.h"
#include "private.h"

#include <ytlib/object_server/id.h>
#include <ytlib/election/leader_channel.h>
#include <ytlib/table_client/chunk_sequence_reader.h>
#include <ytlib/table_client/sync_reader.h>
#include <ytlib/table_client/chunk_sequence_writer.h>
#include <ytlib/table_client/sync_writer.h>
#include <ytlib/chunk_client/remote_reader.h>
#include <ytlib/chunk_client/client_block_cache.h>
#include <ytlib/chunk_server/public.h>

namespace NYT {
namespace NJobProxy {

using namespace NElection;
using namespace NTableClient;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NObjectServer;
using namespace NScheduler::NProto;
using namespace NTableClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = JobProxyLogger;
static NProfiling::TProfiler& Profiler = JobProxyProfiler;

////////////////////////////////////////////////////////////////////////////////

TOrderedMergeJob::TOrderedMergeJob(
    TJobIOConfigPtr ioConfig,
    NElection::TLeaderLookup::TConfigPtr masterConfig,
    const NScheduler::NProto::TMergeJobSpec& jobSpec)
{
    auto blockCache = CreateClientBlockCache(New<TClientBlockCacheConfig>());
    auto masterChannel = CreateLeaderChannel(masterConfig);

    std::vector<TInputChunk> inputChunks;
    FOREACH (const auto& inputSpec, jobSpec.input_spec()) {
        FOREACH (const auto& inputChunk, inputSpec.chunks()) {
            inputChunks.push_back(inputChunk);
        }
    }

    Reader = New<TSyncReaderAdapter>(New<TChunkSequenceReader>(
        ioConfig->ChunkSequenceReader,
        masterChannel,
        blockCache,
        inputChunks));

    // ToDo(psushin): estimate row count for writer.
    auto asyncWriter = New<TChunkSequenceWriter>(
        ioConfig->ChunkSequenceWriter,
        masterChannel,
        TTransactionId::FromProto(jobSpec.output_transaction_id()),
        TChunkListId::FromProto(jobSpec.output_spec().chunk_list_id()),
        ChannelsFromYson(jobSpec.output_spec().channels()));

    Writer = New<TSyncWriterAdapter>(asyncWriter);
}

TJobResult TOrderedMergeJob::Run()
{
    PROFILE_TIMING ("/ordered_merge_time") {
        LOG_INFO("Initializing");
        {
            Reader->Open();
            Writer->Open();
        }
        PROFILE_TIMING_CHECKPOINT("init");

        LOG_INFO("Merging");
        {
            // Unsorted write - use dummy key.
            TNonOwningKey key;
            while (Reader->IsValid()) {
                Writer->WriteRow(Reader->GetRow(), key);
            }
        }
        PROFILE_TIMING_CHECKPOINT("merge");

        LOG_INFO("Finalizing");
        {
            Writer->Close();

            TJobResult result;
            *result.mutable_error() = TError().ToProto();
            return result;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
