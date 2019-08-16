#pragma once

#include "private.h"

#include <yt/server/lib/chunk_pools/chunk_stripe.h>

#include <yt/ytlib/api/native/public.h>

#include <yt/ytlib/chunk_client/public.h>

#include <yt/core/logging/public.h>

#include <yt/client/ypath/rich.h>

namespace NYT::NClickHouseServer {

////////////////////////////////////////////////////////////////////////////////

//! Fetch data slices for given input tables and fill given subquery spec template.
std::vector<NChunkClient::TInputDataSlicePtr> FetchDataSlices(
    NApi::NNative::IClientPtr client,
    const IInvokerPtr& invoker,
    std::vector<NYPath::TRichYPath> inputTablePaths,
    std::optional<DB::KeyCondition> keyCondition,
    NTableClient::TRowBufferPtr rowBuffer,
    TSubqueryConfigPtr config,
    TSubquerySpec& specTemplate);

std::vector<NChunkPools::TChunkStripeListPtr> BuildSubqueries(
    const std::vector<NChunkPools::TChunkStripePtr>& chunkStripes,
    int jobCount,
    std::optional<double> samplingRate,
    TQueryId queryId);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
