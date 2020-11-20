#include "block_input_stream.h"

#include "query_context.h"
#include "host.h"
#include "helpers.h"
#include "config.h"
#include "subquery_spec.h"
#include "batch_conversion.h"

#include <yt/ytlib/api/native/client.h>

#include <yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/ytlib/chunk_client/chunk_reader_statistics.h>
#include <yt/ytlib/chunk_client/parallel_reader_memory_manager.h>

#include <yt/client/table_client/unversioned_row_batch.h>
#include <yt/client/table_client/name_table.h>

#include <yt/core/ytree/yson_serializable.h>

#include <Columns/ColumnNullable.h>
#include <Columns/ColumnVector.h>

#include <DataTypes/DataTypeNothing.h>

#include <Interpreters/ExpressionActions.h>

namespace NYT::NClickHouseServer {

using namespace NTableClient;
using namespace NLogging;
using namespace NConcurrency;
using namespace NTracing;
using namespace NChunkClient;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

namespace {

NTableClient::TTableSchemaPtr FilterColumnsInSchema(
    const NTableClient::TTableSchema& schema,
    DB::Names columnNames)
{
    std::vector<TString> columnNamesInString;
    for (const auto& columnName : columnNames) {
        schema.GetColumnOrThrow(columnName);
        columnNamesInString.emplace_back(columnName);
    }
    return schema.Filter(columnNamesInString);
}

TClientBlockReadOptions CreateBlockReadOptions(const TString& user)
{
    TClientBlockReadOptions blockReadOptions;
    blockReadOptions.ChunkReaderStatistics = New<TChunkReaderStatistics>();
    blockReadOptions.WorkloadDescriptor = TWorkloadDescriptor(EWorkloadCategory::UserRealtime);
    blockReadOptions.WorkloadDescriptor.CompressionFairShareTag = user;
    blockReadOptions.ReadSessionId = NChunkClient::TReadSessionId::Create();
    return blockReadOptions;
}

// Analog of the method from MergeTreeBaseSelectBlockInputStream::executePrewhereActions from CH.
void ExecutePrewhereActions(DB::Block& block, const DB::PrewhereInfoPtr& prewhereInfo)
{
    if (prewhereInfo->alias_actions) {
        prewhereInfo->alias_actions->execute(block);
    }
    prewhereInfo->prewhere_actions->execute(block);
    if (!block) {
        block.insert({nullptr, std::make_shared<DB::DataTypeNothing>(), "_nothing"});
    }
}

DB::Block FilterRowsByPrewhereInfo(
    DB::Block&& blockToFilter,
    const DB::PrewhereInfoPtr& prewhereInfo)
{
    auto columnsWithTypeAndName = blockToFilter.getColumnsWithTypeAndName();

    // Create prewhere column for filtering.
    ExecutePrewhereActions(blockToFilter, prewhereInfo);

    // Extract or materialize filter data.
    // Note that prewhere column is either UInt8 or Nullable(UInt8).
    const DB::IColumn::Filter* filter;
    DB::IColumn::Filter materializedFilter;
    const auto& prewhereColumn = blockToFilter.getByName(prewhereInfo->prewhere_column_name).column;
    if (const auto* nullablePrewhereColumn = DB::checkAndGetColumn<DB::ColumnNullable>(prewhereColumn.get())) {
        const auto* prewhereNullsColumn = DB::checkAndGetColumn<DB::ColumnVector<ui8>>(nullablePrewhereColumn->getNullMapColumn());
        YT_VERIFY(prewhereNullsColumn);
        const auto& prewhereNulls = prewhereNullsColumn->getData();

        const auto* prewhereValuesColumn = DB::checkAndGetColumn<DB::ColumnVector<ui8>>(nullablePrewhereColumn->getNestedColumn());
        YT_VERIFY(prewhereValuesColumn);
        const auto& prewhereValues = prewhereValuesColumn->getData();

        YT_VERIFY(prewhereNulls.size() == prewhereValues.size());
        auto rowCount = prewhereValues.size();
        materializedFilter.resize_exact(rowCount);
        for (size_t index = 0; index < rowCount; ++index) {
            materializedFilter[index] = static_cast<ui8>(prewhereNulls[index] == 0 && prewhereValues[index] != 0);
        }
        filter = &materializedFilter;
    } else {
        const auto* boolPrewhereColumn = DB::checkAndGetColumn<DB::ColumnVector<ui8>>(prewhereColumn.get());
        YT_VERIFY(boolPrewhereColumn);
        filter = &boolPrewhereColumn->getData();
    }

    // Apply filter.
    for (auto& columnWithTypeAndName : columnsWithTypeAndName) {
        columnWithTypeAndName.column = columnWithTypeAndName.column->filter(*filter, 0);
    }
    auto filteredBlock = DB::Block(std::move(columnsWithTypeAndName));

    // Execute prewhere actions for filtered block.
    ExecutePrewhereActions(filteredBlock, prewhereInfo);

    return filteredBlock;
}

}  // namespace

TBlockInputStream::TBlockInputStream(
    ISchemalessMultiChunkReaderPtr reader,
    TTableSchemaPtr readSchema,
    TTraceContextPtr traceContext,
    THost* host,
    TQuerySettingsPtr settings,
    TLogger logger,
    DB::PrewhereInfoPtr prewhereInfo)
    : Reader_(std::move(reader))
    , ReadSchema_(std::move(readSchema))
    , TraceContext_(std::move(traceContext))
    , Host_(host)
    , Settings_(std::move(settings))
    , Logger(std::move(logger))
    , RowBuffer_(New<NTableClient::TRowBuffer>())
    , PrewhereInfo_(std::move(prewhereInfo))
{
    Prepare();
}

std::string TBlockInputStream::getName() const
{
    return "BlockInputStream";
}

DB::Block TBlockInputStream::getHeader() const
{
    return OutputHeaderBlock_;
}

void TBlockInputStream::readPrefixImpl()
{
    TTraceContextGuard guard(TraceContext_);
    YT_LOG_DEBUG("readPrefixImpl() is called");

    IdleTimer_.Start();
}

void TBlockInputStream::readSuffixImpl()
{
    TTraceContextGuard guard(TraceContext_);
    YT_LOG_DEBUG("readSuffixImpl() is called");

    IdleTimer_.Stop();

    YT_LOG_DEBUG(
        "Block input stream timing statistics (ConversionCpuTime: %v, ConversionSyncWaitTime: %v, IdleTime: %v, ReadCount: %v)",
        ConversionCpuTime_,
        ConversionSyncWaitTime_,
        IdleTimer_.GetElapsedTime(),
        ReadCount_);

    if (TraceContext_) {
        TraceContext_->AddTag("chyt.reader.data_statistics", ToString(Reader_->GetDataStatistics()));
        TraceContext_->AddTag("chyt.reader.codec_statistics", ToString(Reader_->GetDecompressionStatistics()));
        TraceContext_->AddTag("chyt.reader.idle_time", ToString(IdleTimer_.GetElapsedTime()));
        TraceContext_->Finish();
    }
}

DB::Block TBlockInputStream::readImpl()
{
    TTraceContextGuard guard(TraceContext_);

    TNullTraceContextGuard nullGuard;
    if (Settings_->EnableReaderTracing) {
        nullGuard.Release();
    }

    IdleTimer_.Stop();
    ++ReadCount_;

    NProfiling::TWallTimer totalWallTimer;
    YT_LOG_TRACE("Started reading ClickHouse block");

    DB::Block block;
    while (block.rows() == 0) {
        TRowBatchReadOptions options{
            // .MaxRowsPerRead = 100 * 1000,
            // .MaxDataWeightPerRead = 160_MB,
            .Columnar = Settings_->EnableColumnarRead,
        };
        auto batch = Reader_->Read(options);
        if (!batch) {
            return {};
        }
        if (batch->IsEmpty()) {
            NProfiling::TWallTimer wallTimer;
            WaitFor(Reader_->GetReadyEvent())
                .ThrowOnError();
            auto elapsed = wallTimer.GetElapsedTime();
            if (elapsed > TDuration::Seconds(1)) {
                YT_LOG_DEBUG("Reading took significant time (WallTime: %v)", elapsed);
            }
            continue;
        }

        {
            if (Settings_->ConvertRowBatchesInWorkerThreadPool) {
                auto start = TInstant::Now();
                block = WaitFor(BIND(&TBlockInputStream::ConvertRowBatchToBlock, this, batch)
                    .AsyncVia(Host_->GetClickHouseWorkerInvoker())
                    .Run())
                    .ValueOrThrow();
                auto finish = TInstant::Now();
                ConversionSyncWaitTime_ += finish - start;
            } else {
                block = ConvertRowBatchToBlock(batch);
            }
        }

        if (PrewhereInfo_) {
            block = FilterRowsByPrewhereInfo(std::move(block), PrewhereInfo_);
        }

        // NB: ConvertToField copies all strings, so clearing row buffer is safe here.
        RowBuffer_->Clear();
    }

    auto totalElapsed = totalWallTimer.GetElapsedTime();
    YT_LOG_TRACE("Finished reading ClickHouse block (WallTime: %v)", totalElapsed);

    IdleTimer_.Start();

    return block;
}

void TBlockInputStream::Prepare()
{
    InputHeaderBlock_ = ToHeaderBlock(*ReadSchema_, Settings_->Composite);
    OutputHeaderBlock_ = ToHeaderBlock(*ReadSchema_, Settings_->Composite);

    if (PrewhereInfo_) {
        // Create header with executed prewhere actions.
        ExecutePrewhereActions(OutputHeaderBlock_, PrewhereInfo_);
    }

    for (int index = 0; index < static_cast<int>(ReadSchema_->Columns().size()); ++index) {
        const auto& columnSchema = ReadSchema_->Columns()[index];
        auto id = Reader_->GetNameTable()->GetIdOrRegisterName(columnSchema.Name());
        if (static_cast<int>(IdToColumnIndex_.size()) <= id) {
            IdToColumnIndex_.resize(id + 1, -1);
        }
        IdToColumnIndex_[id] = index;
    }
}

DB::Block TBlockInputStream::ConvertRowBatchToBlock(const IUnversionedRowBatchPtr& batch)
{
    NProfiling::TWallTimer timer;
    auto result = NClickHouseServer::ConvertRowBatchToBlock(
        batch,
        *ReadSchema_,
        IdToColumnIndex_,
        RowBuffer_,
        InputHeaderBlock_,
        Settings_->Composite);

    ConversionCpuTime_ += timer.GetElapsedTime();

    return result;
}

////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<TBlockInputStream> CreateBlockInputStream(
    ISchemalessMultiChunkReaderPtr reader,
    TTableSchemaPtr readSchema,
    TTraceContextPtr traceContext,
    THost* host,
    TQuerySettingsPtr querySettings,
    TLogger logger,
    DB::PrewhereInfoPtr prewhereInfo)
{
    return std::make_shared<TBlockInputStream>(
        std::move(reader),
        std::move(readSchema),
        std::move(traceContext),
        host,
        querySettings,
        logger,
        std::move(prewhereInfo));
}

////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<TBlockInputStream> CreateBlockInputStream(
    TStorageContext* storageContext,
    const TSubquerySpec& subquerySpec,
    const DB::Names& columnNames,
    const NTracing::TTraceContextPtr& traceContext,
    const std::vector<TDataSliceDescriptor>& dataSliceDescriptors,
    DB::PrewhereInfoPtr prewhereInfo)
{
    auto* queryContext = storageContext->QueryContext;
    auto schema = FilterColumnsInSchema(*subquerySpec.ReadSchema, columnNames);
    auto blockReadOptions = CreateBlockReadOptions(queryContext->User);

    auto blockInputStreamTraceContext = NTracing::CreateChildTraceContext(
        traceContext,
        "ClickHouseYt.BlockInputStream");
    
    NTracing::TTraceContextGuard guard(blockInputStreamTraceContext);
    // Readers capture context implicitly, so create NullTraceContextGuard if tracing is disabled.
    NTracing::TNullTraceContextGuard nullGuard;
    if (storageContext->Settings->EnableReaderTracing) {
        nullGuard.Release();
    }

    ISchemalessMultiChunkReaderPtr reader;

    auto readerMemoryManager = queryContext->Host->GetMultiReaderMemoryManager()->CreateMultiReaderMemoryManager(
        queryContext->Host->GetConfig()->ReaderMemoryRequirement,
        {queryContext->UserTagId});

    auto defaultTableReaderConfig = queryContext->Host->GetConfig()->TableReader;
    auto tableReaderConfig = storageContext->Settings->TableReader;
    tableReaderConfig = UpdateYsonSerializable(defaultTableReaderConfig, ConvertToNode(tableReaderConfig));

    tableReaderConfig->SamplingMode = subquerySpec.TableReaderConfig->SamplingMode;
    tableReaderConfig->SamplingRate = subquerySpec.TableReaderConfig->SamplingRate;
    tableReaderConfig->SamplingSeed = subquerySpec.TableReaderConfig->SamplingSeed;

    if (!subquerySpec.DataSourceDirectory->DataSources().empty() &&
        subquerySpec.DataSourceDirectory->DataSources()[0].GetType() == EDataSourceType::VersionedTable)
    {
        std::vector<NChunkClient::NProto::TChunkSpec> chunkSpecs;
        for (const auto& dataSliceDescriptor : dataSliceDescriptors) {
            for (auto& chunkSpec : dataSliceDescriptor.ChunkSpecs) {
                chunkSpecs.emplace_back(std::move(chunkSpec));
            }
        }
        TDataSliceDescriptor dataSliceDescriptor(std::move(chunkSpecs));

        reader = CreateSchemalessMergingMultiChunkReader(
            std::move(tableReaderConfig),
            New<NTableClient::TTableReaderOptions>(),
            queryContext->Client(),
            /* localDescriptor */ {},
            /* partitionTag */ std::nullopt,
            queryContext->Client()->GetNativeConnection()->GetBlockCache(),
            queryContext->Client()->GetNativeConnection()->GetNodeDirectory(),
            subquerySpec.DataSourceDirectory,
            dataSliceDescriptor,
            TNameTable::FromSchema(*schema),
            blockReadOptions,
            TColumnFilter(schema->Columns().size()),
            /* trafficMeter */ nullptr,
            GetUnlimitedThrottler(),
            GetUnlimitedThrottler(),
            /* multiReaderMemoryManager = */readerMemoryManager);
    } else {
        reader = CreateSchemalessParallelMultiReader(
            std::move(tableReaderConfig),
            New<NTableClient::TTableReaderOptions>(),
            queryContext->Client(),
            /* localDescriptor =*/{},
            std::nullopt,
            queryContext->Client()->GetNativeConnection()->GetBlockCache(),
            queryContext->Client()->GetNativeConnection()->GetNodeDirectory(),
            subquerySpec.DataSourceDirectory,
            dataSliceDescriptors,
            TNameTable::FromSchema(*schema),
            blockReadOptions,
            TColumnFilter(schema->Columns().size()),
            /* keyColumns =*/{},
            /* partitionTag =*/std::nullopt,
            /* trafficMeter =*/nullptr,
            /* bandwidthThrottler =*/GetUnlimitedThrottler(),
            /* rpsThrottler =*/GetUnlimitedThrottler(),
            /* multiReaderMemoryManager =*/readerMemoryManager);
    }

    return CreateBlockInputStream(
        reader,
        schema,
        blockInputStreamTraceContext,
        queryContext->Host,
        storageContext->Settings,
        TLogger(queryContext->Logger)
            .AddTag("ReadSessionId: %v", blockReadOptions.ReadSessionId),
        prewhereInfo);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
