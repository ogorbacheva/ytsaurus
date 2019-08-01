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
    const DB::KeyCondition* keyCondition,
    NTableClient::TRowBufferPtr rowBuffer,
    TSubqueryConfigPtr config,
    TSubquerySpec& specTemplate);

NChunkPools::TChunkStripeListPtr BuildThreadStripes(
    const std::vector<NChunkClient::TInputDataSlicePtr>& dataSlices,
    int jobCount,
    std::optional<double> samplingRate,
    TQueryId queryId);

void FillDataSliceDescriptors(TSubquerySpec& subquerySpec, const TRange<NChunkPools::TChunkStripePtr>& chunkStripes);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
