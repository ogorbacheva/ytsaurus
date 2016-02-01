#pragma once

#include "public.h"

#include <yt/ytlib/table_client/public.h>

#include <yt/core/actions/public.h>

#include <yt/core/misc/range.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

//! Creates a reader that merges data from the relevant stores and
//! returns a single version of each value.
NTableClient::ISchemafulReaderPtr CreateSchemafulTabletReader(
    TTabletSnapshotPtr tabletSnapshot,
    const TColumnFilter& columnFilter,
    TOwningKey lowerBound,
    TOwningKey upperBound,
    TTimestamp timestamp);

NTableClient::ISchemafulReaderPtr CreateSchemafulTabletReader(
    TTabletSnapshotPtr tabletSnapshot,
    const TColumnFilter& columnFilter,    
    TPartitionSnapshotPtr paritionSnapshot,
    const TSharedRange<TKey>& keys,
    TTimestamp timestamp);

NTableClient::ISchemafulReaderPtr CreateSchemafulTabletReader(
    TTabletSnapshotPtr tabletSnapshot,
    const TColumnFilter& columnFilter,
    const TSharedRange<TKey>& keys,
    TTimestamp timestamp,
    int concurrency,
    NTableClient::TRowBufferPtr rowBuffer = nullptr);

//! Creates a reader that merges data from all given #stores and
//! returns all versions of each value.
NTableClient::IVersionedReaderPtr CreateVersionedTabletReader(
    IInvokerPtr poolInvoker,
    TTabletSnapshotPtr tabletSnapshot,
    std::vector<IStorePtr> stores,
    TOwningKey lowerBound,
    TOwningKey upperBound,
    TTimestamp currentTimestamp,
    TTimestamp majorTimestamp);

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
