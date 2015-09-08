#pragma once

#include <ytlib/tablet_client/public.h>

#include <ytlib/new_table_client/public.h>

#include <ytlib/node_tracker_client/public.h>

// TODO(babenko): kill this when refactoring TDataSplit
#include <ytlib/chunk_client/public.h>

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

class TExpression;
class TGroupClause;
class TProjectClause;
class TJoinClause;
class TQuery;
class TPlanFragment;
class TQueryStatistics;

} // namespace NProto

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(TExpression)
typedef TIntrusivePtr<const TExpression> TConstExpressionPtr;

DECLARE_REFCOUNTED_STRUCT(TJoinClause)
typedef TIntrusivePtr<const TJoinClause> TConstJoinClausePtr;

DECLARE_REFCOUNTED_STRUCT(TGroupClause)
typedef TIntrusivePtr<const TGroupClause> TConstGroupClausePtr;

DECLARE_REFCOUNTED_STRUCT(TOrderClause)
typedef TIntrusivePtr<const TOrderClause> TConstOrderClausePtr;

DECLARE_REFCOUNTED_STRUCT(TProjectClause)
typedef TIntrusivePtr<const TProjectClause> TConstProjectClausePtr;

DECLARE_REFCOUNTED_STRUCT(TQuery);
typedef TIntrusivePtr<const TQuery> TConstQueryPtr;

DECLARE_REFCOUNTED_STRUCT(TPlanFragment);
typedef TIntrusivePtr<const TPlanFragment> TConstPlanFragmentPtr;

struct IPrepareCallbacks;

struct TQueryStatistics;

DECLARE_REFCOUNTED_STRUCT(IFunctionDescriptor)

DECLARE_REFCOUNTED_STRUCT(IAggregateFunctionDescriptor)

DECLARE_REFCOUNTED_STRUCT(ICallingConvention)

DECLARE_REFCOUNTED_STRUCT(IExecutor)

DECLARE_REFCOUNTED_CLASS(TEvaluator)

DECLARE_REFCOUNTED_CLASS(TExecutorConfig)

DECLARE_REFCOUNTED_CLASS(TColumnEvaluator)

DECLARE_REFCOUNTED_CLASS(TColumnEvaluatorCache)

DECLARE_REFCOUNTED_CLASS(TColumnEvaluatorCacheConfig)

DECLARE_REFCOUNTED_STRUCT(IFunctionRegistry)

DECLARE_REFCOUNTED_CLASS(TFunctionRegistry)

// TODO(babenko): kill this when refactoring TDataSplit
typedef NChunkClient::NProto::TChunkSpec TDataSplit;
typedef std::vector<TDataSplit> TDataSplits;

using NVersionedTableClient::ISchemafulReader;
using NVersionedTableClient::ISchemafulReaderPtr;
using NVersionedTableClient::ISchemafulWriter;
using NVersionedTableClient::ISchemafulWriterPtr;
using NVersionedTableClient::EValueType;
using NVersionedTableClient::TTableSchema;
using NVersionedTableClient::TColumnSchema;
using NVersionedTableClient::TKeyColumns;

using NTransactionClient::TTimestamp;
using NTransactionClient::NullTimestamp;

using NVersionedTableClient::TRowBuffer;
using NVersionedTableClient::TRowBufferPtr;

using NNodeTrackerClient::TNodeDirectoryPtr;

typedef NVersionedTableClient::TUnversionedRow TRow;
typedef NVersionedTableClient::TUnversionedRowHeader TRowHeader;
typedef NVersionedTableClient::TUnversionedValue TValue;
typedef NVersionedTableClient::TUnversionedOwningValue TOwningValue;
typedef NVersionedTableClient::TUnversionedValueData TValueData;
typedef NVersionedTableClient::TUnversionedOwningRow TOwningRow;
typedef NVersionedTableClient::TUnversionedRowBuilder TRowBuilder;
typedef NVersionedTableClient::TUnversionedOwningRowBuilder TOwningRowBuilder;
typedef NVersionedTableClient::TOwningKey TKey;

typedef std::pair<TKey, TKey> TKeyRange;
typedef std::pair<TRow, TRow> TRowRange;
typedef std::vector<TRowRange> TRowRanges;

struct TDataSource;

const int MaxRowsPerRead = 1024;
const int MaxRowsPerWrite = 1024;

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

