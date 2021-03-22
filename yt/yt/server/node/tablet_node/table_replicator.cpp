#include "hint_manager.h"
#include "private.h"
#include "table_replicator.h"
#include "tablet.h"
#include "tablet_manager.h"
#include "tablet_reader.h"
#include "tablet_slot.h"
#include "tablet_snapshot_store.h"
#include "transaction_manager.h"

#include <yt/yt/server/lib/tablet_node/proto/tablet_manager.pb.h>

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_options.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_statistics.h>

#include <yt/yt/ytlib/hive/cluster_directory.h>

#include <yt/yt/ytlib/transaction_client/action.h>

#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/transaction.h>

#include <yt/yt/ytlib/security_client/public.h>

#include <yt/yt/client/table_client/unversioned_row.h>
#include <yt/yt/client/table_client/unversioned_reader.h>
#include <yt/yt/client/table_client/row_batch.h>
#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/name_table.h>
#include <yt/yt/client/table_client/helpers.h>

#include <yt/yt/client/tablet_client/table_mount_cache.h>

#include <yt/yt/client/api/transaction.h>

#include <yt/yt/client/transaction_client/helpers.h>
#include <yt/yt/client/transaction_client/timestamp_provider.h>

#include <yt/yt/client/misc/workload.h>

#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/delayed_executor.h>
#include <yt/yt/core/concurrency/throughput_throttler.h>

#include <yt/yt/core/misc/finally.h>

namespace NYT::NTabletNode {

using namespace NApi;
using namespace NChunkClient;
using namespace NConcurrency;
using namespace NHiveClient;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTransactionClient;
using namespace NYPath;
using namespace NHydra;
using namespace NProfiling;

////////////////////////////////////////////////////////////////////////////////

static const int TabletRowsPerRead = 1000;
static const auto HardErrorAttribute = TErrorAttribute("hard", true);

////////////////////////////////////////////////////////////////////////////////

class TTableReplicator::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TTabletManagerConfigPtr config,
        TTablet* tablet,
        TTableReplicaInfo* replicaInfo,
        NNative::IConnectionPtr localConnection,
        TTabletSlotPtr slot,
        ITabletSnapshotStorePtr tabletSnapshotStore,
        IHintManagerPtr hintManager,
        IInvokerPtr workerInvoker,
        IThroughputThrottlerPtr nodeInThrottler,
        IThroughputThrottlerPtr nodeOutThrottler)
        : Config_(std::move(config))
        , LocalConnection_(std::move(localConnection))
        , Slot_(std::move(slot))
        , TabletSnapshotStore_(std::move(tabletSnapshotStore))
        , HintManager_(std::move(hintManager))
        , WorkerInvoker_(std::move(workerInvoker))
        , TabletId_(tablet->GetId())
        , MountRevision_(tablet->GetMountRevision())
        , TableSchema_(tablet->GetTableSchema())
        , NameTable_(TNameTable::FromSchema(*TableSchema_))
        , ReplicaId_(replicaInfo->GetId())
        , ClusterName_(replicaInfo->GetClusterName())
        , ReplicaPath_(replicaInfo->GetReplicaPath())
        , MountConfig_(tablet->GetConfig())
        , PreserveTabletIndex_(MountConfig_->PreserveTabletIndex)
        , TabletIndexColumnId_(TableSchema_->ToReplicationLog()->GetColumnCount() + 1) /* maxColumnId - 1(timestamp) + 3(header size)*/
        , Logger(TabletNodeLogger.WithTag("%v, ReplicaId: %v",
            tablet->GetLoggingTag(),
            ReplicaId_))
        , NodeInThrottler_(std::move(nodeInThrottler))
        , Throttler_(CreateCombinedThrottler(std::vector<IThroughputThrottlerPtr>{
            std::move(nodeOutThrottler),
            CreateReconfigurableThroughputThrottler(MountConfig_->ReplicationThrottler, Logger)}))
    { }

    void Enable()
    {
        Disable();

        FiberFuture_ = BIND(&TImpl::FiberMain, MakeWeak(this))
            .AsyncVia(Slot_->GetHydraManager()->GetAutomatonCancelableContext()->CreateInvoker(WorkerInvoker_))
            .Run();

        YT_LOG_INFO("Replicator fiber started");
    }

    void Disable()
    {
        if (FiberFuture_) {
            FiberFuture_.Cancel(TError("Replicator disabled"));
            YT_LOG_INFO("Replicator fiber stopped");
        }
        FiberFuture_.Reset();
    }

private:
    const TTabletManagerConfigPtr Config_;
    const NNative::IConnectionPtr LocalConnection_;
    const TTabletSlotPtr Slot_;
    const ITabletSnapshotStorePtr TabletSnapshotStore_;
    const IHintManagerPtr HintManager_;
    const IInvokerPtr WorkerInvoker_;

    const TTabletId TabletId_;
    const TRevision MountRevision_;
    const TTableSchemaPtr TableSchema_;
    const TNameTablePtr NameTable_;
    const TTableReplicaId ReplicaId_;
    const TString ClusterName_;
    const TYPath ReplicaPath_;
    const TTableMountConfigPtr MountConfig_;
    const bool PreserveTabletIndex_;
    const int TabletIndexColumnId_;

    const NLogging::TLogger Logger;

    const IThroughputThrottlerPtr NodeInThrottler_;
    const IThroughputThrottlerPtr Throttler_;

    TFuture<void> FiberFuture_;

    void FiberMain()
    {
        while (true) {
            NProfiling::TWallTimer timer;
            FiberIteration();
            TDelayedExecutor::WaitForDuration(MountConfig_->ReplicationTickPeriod - timer.GetElapsedTime());
        }
    }

    void FiberIteration()
    {
        TTableReplicaSnapshotPtr replicaSnapshot;
        try {
            auto tabletSnapshot = TabletSnapshotStore_->FindTabletSnapshot(TabletId_, MountRevision_);
            if (!tabletSnapshot) {
                THROW_ERROR_EXCEPTION("No tablet snapshot is available")
                    << HardErrorAttribute;
            }

            replicaSnapshot = tabletSnapshot->FindReplicaSnapshot(ReplicaId_);
            if (!replicaSnapshot) {
                THROW_ERROR_EXCEPTION("No table replica snapshot is available")
                    << HardErrorAttribute;
            }

            auto alienConnection = LocalConnection_->GetClusterDirectory()->FindConnection(ClusterName_);
            if (!alienConnection) {
                THROW_ERROR_EXCEPTION("Replica cluster %Qv is not known", ClusterName_)
                    << HardErrorAttribute;
            }


            const auto& tabletRuntimeData = tabletSnapshot->TabletRuntimeData;
            const auto& replicaRuntimeData = replicaSnapshot->RuntimeData;
            const auto& counters = replicaSnapshot->Counters;
            auto countError = Finally([&] {
                if (std::uncaught_exception()) {
                    counters.ReplicationErrorCount.Increment();
                }
            });

            {
                auto throttleFuture = Throttler_->Throttle(1);
                if (!throttleFuture.IsSet()) {
                    TEventTimer timerGuard(counters.ReplicationTransactionCommitTime);
                    YT_LOG_DEBUG("Started waiting for replication throttling");
                    WaitFor(throttleFuture)
                        .ThrowOnError();
                    YT_LOG_DEBUG("Finished waiting for replication throttling");
                }
            }

            // YT-8542: Fetch the last barrier timestamp _first_ to ensure proper serialization between
            // replicator and tablet slot threads.
            auto lastBarrierTimestamp = Slot_->GetRuntimeData()->BarrierTimestamp.load();
            auto lastReplicationRowIndex = replicaRuntimeData->CurrentReplicationRowIndex.load();
            auto lastReplicationTimestamp = replicaRuntimeData->LastReplicationTimestamp.load();
            auto totalRowCount = tabletRuntimeData->TotalRowCount.load();
            if (replicaRuntimeData->PreparedReplicationRowIndex > lastReplicationRowIndex) {
                // Some log rows are prepared for replication, hence replication cannot proceed.
                // Seeing this is not typical since we're waiting for the replication commit to complete (see below).
                // However we may occasionally run into this check on epoch change or when commit times out
                // due to broken replica participant.
                replicaRuntimeData->Error.Store(TError());
                return;
            }

            auto updateCountersGuard = Finally([&] {
                auto rowCount = std::max(
                    static_cast<i64>(0),
                    tabletRuntimeData->TotalRowCount.load() - replicaRuntimeData->CurrentReplicationRowIndex.load());
                const auto& timestampProvider = LocalConnection_->GetTimestampProvider();
                auto time = (rowCount == 0)
                    ? TDuration::Zero()
                    : TimestampToInstant(timestampProvider->GetLatestTimestamp()).second - TimestampToInstant(replicaRuntimeData->CurrentReplicationTimestamp).first;

                counters.LagRowCount.Record(rowCount);
                counters.LagTime.Update(time);
            });

            if (HintManager_->IsReplicaClusterBanned(ClusterName_)) {
                YT_LOG_DEBUG("Skipping table replication iteration due to ban of replica cluster (ClusterName: %v)",
                    ClusterName_);
                return;
            }

            auto isVersioned = TableSchema_->IsSorted() && replicaRuntimeData->PreserveTimestamps;

            if (totalRowCount <= lastReplicationRowIndex) {
                // All committed rows are replicated.
                if (lastReplicationTimestamp < lastBarrierTimestamp) {
                    replicaRuntimeData->LastReplicationTimestamp.store(lastBarrierTimestamp);
                }
                replicaRuntimeData->Error.Store(TError());
                return;
            }

            NNative::ITransactionPtr localTransaction;
            ITransactionPtr alienTransaction;
            {
                TEventTimer timerGuard(counters.ReplicationTransactionStartTime);

                YT_LOG_DEBUG("Starting replication transactions");

                auto localClient = LocalConnection_->CreateNativeClient(TClientOptions::FromUser(NSecurityClient::ReplicatorUserName));
                localTransaction = WaitFor(localClient->StartNativeTransaction(ETransactionType::Tablet))
                    .ValueOrThrow();

                auto alienClient = alienConnection->CreateClient(TClientOptions::FromUser(NSecurityClient::ReplicatorUserName));

                TAlienTransactionStartOptions transactionStartOptions;
                if (!isVersioned) {
                    transactionStartOptions.Atomicity = replicaRuntimeData->Atomicity;
                }

                alienTransaction = WaitFor(StartAlienTransaction(localTransaction, alienClient, transactionStartOptions))
                    .ValueOrThrow();

                YT_LOG_DEBUG("Replication transactions started (TransactionId: %v)",
                    localTransaction->GetId());
            }

            TRowBufferPtr rowBuffer;
            std::vector<TRowModification> replicationRows;

            i64 startRowIndex = lastReplicationRowIndex;
            bool checkPrevReplicationRowIndex = true;
            i64 newReplicationRowIndex;
            TTimestamp newReplicationTimestamp;
            i64 batchRowCount;
            i64 batchDataWeight;

            // TODO(savrus): profile chunk reader statistics.
            TClientChunkReadOptions chunkReadOptions{
                .WorkloadDescriptor = TWorkloadDescriptor(EWorkloadCategory::SystemTabletReplication),
                .ReadSessionId = TReadSessionId::Create()
            };

            {
                TEventTimer timerGuard(counters.ReplicationRowsReadTime);
                auto readReplicationBatch = [&]() {
                    return ReadReplicationBatch(
                        MountConfig_,
                        tabletSnapshot,
                        replicaSnapshot,
                        chunkReadOptions,
                        startRowIndex,
                        &replicationRows,
                        &rowBuffer,
                        &newReplicationRowIndex,
                        &newReplicationTimestamp,
                        &batchRowCount,
                        &batchDataWeight,
                        isVersioned);
                };

                if (!readReplicationBatch()) {
                    checkPrevReplicationRowIndex = false;
                    startRowIndex = ComputeStartRowIndex(
                        MountConfig_,
                        tabletSnapshot,
                        replicaSnapshot,
                        chunkReadOptions);
                    YT_VERIFY(readReplicationBatch());
                }
            }

            {
                TEventTimer timerGuard(counters.ReplicationRowsWriteTime);

                TModifyRowsOptions options;
                options.UpstreamReplicaId = ReplicaId_;
                alienTransaction->ModifyRows(
                    ReplicaPath_,
                    NameTable_,
                    MakeSharedRange(std::move(replicationRows), std::move(rowBuffer)),
                    options);
            }

            {
                NProto::TReqReplicateRows req;
                ToProto(req.mutable_tablet_id(), TabletId_);
                ToProto(req.mutable_replica_id(), ReplicaId_);
                if (checkPrevReplicationRowIndex) {
                    req.set_prev_replication_row_index(startRowIndex);
                }
                req.set_new_replication_row_index(newReplicationRowIndex);
                req.set_new_replication_timestamp(newReplicationTimestamp);
                localTransaction->AddAction(Slot_->GetCellId(), MakeTransactionActionData(req));
            }

            {
                TEventTimer timerGuard(counters.ReplicationTransactionCommitTime);
                YT_LOG_DEBUG("Started committing replication transaction");

                TTransactionCommitOptions commitOptions;
                commitOptions.CoordinatorCellId = Slot_->GetCellId();
                commitOptions.Force2PC = true;
                commitOptions.CoordinatorCommitMode = ETransactionCoordinatorCommitMode::Lazy;
                commitOptions.GeneratePrepareTimestamp = !replicaRuntimeData->PreserveTimestamps;
                WaitFor(localTransaction->Commit(commitOptions))
                    .ThrowOnError();

                YT_LOG_DEBUG("Finished committing replication transaction");
            }

            if (lastReplicationTimestamp > newReplicationTimestamp) {
                YT_LOG_ERROR("Non-monotonic change to last replication timestamp attempted; ignored (LastReplicationTimestamp: %llx -> %llx)",
                    lastReplicationTimestamp,
                    newReplicationTimestamp);
            } else {
                replicaRuntimeData->LastReplicationTimestamp.store(newReplicationTimestamp);
            }
            replicaRuntimeData->Error.Store(TError());

            counters.ReplicationBatchRowCount.Record(batchRowCount);
            counters.ReplicationBatchDataWeight.Record(batchDataWeight);
            counters.ReplicationRowCount.Increment(batchRowCount);
            counters.ReplicationDataWeight.Increment(batchDataWeight);
        } catch (const std::exception& ex) {
            TError error(ex);
            if (replicaSnapshot) {
                replicaSnapshot->RuntimeData->Error.Store(
                    error << TErrorAttribute("tablet_id", TabletId_));
            }
            if (error.Attributes().Get<bool>("hard", false)) {
                DoHardBackoff(error);
            } else {
                DoSoftBackoff(error);
            }
        }
    }

    TTimestamp ReadLogRowTimestamp(
        const TTableMountConfigPtr& mountConfig,
        const TTabletSnapshotPtr& tabletSnapshot,
        const TClientChunkReadOptions& chunkReadOptions,
        i64 rowIndex)
    {
        auto reader = CreateSchemafulRangeTabletReader(
            tabletSnapshot,
            TColumnFilter(),
            MakeRowBound(rowIndex),
            MakeRowBound(rowIndex + 1),
            NullTimestamp,
            chunkReadOptions,
            /* tabletThrottlerKind */ std::nullopt,
            NodeInThrottler_);

        TRowBatchReadOptions readOptions{
            .MaxRowsPerRead = 1
        };

        IUnversionedRowBatchPtr batch;
        while (true) {
            batch = reader->Read(readOptions);
            if (!batch) {
                THROW_ERROR_EXCEPTION("Missing row %v in replication log of tablet %v",
                    rowIndex,
                    tabletSnapshot->TabletId)
                    << HardErrorAttribute;
            }

            if (batch->IsEmpty()) {
                YT_LOG_DEBUG(
                    "Waiting for log row from tablet reader (RowIndex: %v)",
                    rowIndex);
                WaitFor(reader->GetReadyEvent())
                    .ThrowOnError();
                continue;
            }

            // One row is enough.
            break;
        }

        auto readerRows = batch->MaterializeRows();
        YT_VERIFY(readerRows.size() == 1);

        i64 actualRowIndex = GetRowIndex(readerRows[0]);
        TTimestamp timestamp = GetTimestamp(readerRows[0]);
        YT_VERIFY(actualRowIndex == rowIndex);

        YT_LOG_DEBUG("Replication log row timestamp is read (RowIndex: %v, Timestamp: %llx)",
            rowIndex,
            timestamp);

        return timestamp;
    }

    i64 ComputeStartRowIndex(
        const TTableMountConfigPtr& mountConfig,
        const TTabletSnapshotPtr& tabletSnapshot,
        const TTableReplicaSnapshotPtr& replicaSnapshot,
        const TClientChunkReadOptions& chunkReadOptions)
    {
        auto trimmedRowCount = tabletSnapshot->TabletRuntimeData->TrimmedRowCount.load();
        auto totalRowCount = tabletSnapshot->TabletRuntimeData->TotalRowCount.load();

        auto rowIndexLo = trimmedRowCount;
        auto rowIndexHi = totalRowCount;
        if (rowIndexLo == rowIndexHi) {
            THROW_ERROR_EXCEPTION("No replication log rows are available")
                << HardErrorAttribute;
        }

        auto startReplicationTimestamp = replicaSnapshot->StartReplicationTimestamp;

        YT_LOG_DEBUG("Started computing replication start row index (StartReplicationTimestamp: %llx, RowIndexLo: %v, RowIndexHi: %v)",
            startReplicationTimestamp,
            rowIndexLo,
            rowIndexHi);

        while (rowIndexLo < rowIndexHi - 1) {
            auto rowIndexMid = rowIndexLo + (rowIndexHi - rowIndexLo) / 2;
            auto timestampMid = ReadLogRowTimestamp(mountConfig, tabletSnapshot, chunkReadOptions, rowIndexMid);
            if (timestampMid <= startReplicationTimestamp) {
                rowIndexLo = rowIndexMid;
            } else {
                rowIndexHi = rowIndexMid;
            }
        }

        auto startRowIndex = rowIndexLo;
        auto startTimestamp = NullTimestamp;
        while (startRowIndex < totalRowCount) {
            startTimestamp = ReadLogRowTimestamp(mountConfig, tabletSnapshot, chunkReadOptions, startRowIndex);
            if (startTimestamp > startReplicationTimestamp) {
                break;
            }
            ++startRowIndex;
        }

        YT_LOG_DEBUG("Finished computing replication start row index (StartRowIndex: %v, StartTimestamp: %llx)",
            startRowIndex,
            startTimestamp);

        return startRowIndex;
    }

    bool ReadReplicationBatch(
        const TTableMountConfigPtr& mountConfig,
        const TTabletSnapshotPtr& tabletSnapshot,
        const TTableReplicaSnapshotPtr& replicaSnapshot,
        const TClientChunkReadOptions& chunkReadOptions,
        i64 startRowIndex,
        std::vector<TRowModification>* replicationRows,
        TRowBufferPtr* rowBuffer,
        i64* newReplicationRowIndex,
        TTimestamp* newReplicationTimestamp,
        i64* batchRowCount,
        i64* batchDataWeight,
        bool isVersioned)
    {
        auto sessionId = TReadSessionId::Create();
        YT_LOG_DEBUG("Started building replication batch (StartRowIndex: %v, ReadSessionId: %v)",
            startRowIndex,
            sessionId);

        auto reader = CreateSchemafulRangeTabletReader(
            tabletSnapshot,
            TColumnFilter(),
            MakeRowBound(startRowIndex),
            MakeRowBound(std::numeric_limits<i64>::max()),
            NullTimestamp,
            chunkReadOptions,
            /* tabletThrottlerKind */ std::nullopt,
            NodeInThrottler_);

        int timestampCount = 0;
        int rowCount = 0;
        i64 currentRowIndex = startRowIndex;
        i64 dataWeight = 0;

        *rowBuffer = New<TRowBuffer>();
        replicationRows->clear();

        std::vector<TUnversionedRow> readerRows;
        readerRows.reserve(TabletRowsPerRead);

        // This default only makes sense if the batch turns out to be empty.
        auto prevTimestamp = replicaSnapshot->RuntimeData->CurrentReplicationTimestamp.load();

        // Throttling control.
        i64 dataWeightToThrottle = 0;
        auto acquireThrottler = [&] () {
            Throttler_->Acquire(dataWeightToThrottle);
            dataWeightToThrottle = 0;
        };
        auto isThrottlerOverdraft = [&] {
            if (!Throttler_->IsOverdraft()) {
                return false;
            }
            YT_LOG_DEBUG("Bandwidth limit reached; interrupting batch (QueueTotalCount: %v)",
                Throttler_->GetQueueTotalCount());
            return true;
        };

        bool tooMuch = false;

        while (!tooMuch) {
            auto batch = reader->Read();
            if (!batch) {
                break;
            }

            if (batch->IsEmpty()) {
                YT_LOG_DEBUG("Waiting for replicated rows from tablet reader (StartRowIndex: %v)",
                    currentRowIndex);
                WaitFor(reader->GetReadyEvent())
                    .ThrowOnError();
                continue;
            }

            auto readerRows = batch->MaterializeRows();

            YT_LOG_DEBUG("Got replicated rows from tablet reader (StartRowIndex: %v, RowCount: %v)",
                currentRowIndex,
                readerRows.size());

            for (auto row : readerRows) {
                TTypeErasedRow replicationRow;
                ERowModificationType modificationType;
                i64 rowIndex;
                TTimestamp timestamp;

                ParseLogRow(
                    tabletSnapshot,
                    mountConfig,
                    row,
                    *rowBuffer,
                    &replicationRow,
                    &modificationType,
                    &rowIndex,
                    &timestamp,
                    isVersioned);

                if (timestamp <= replicaSnapshot->StartReplicationTimestamp) {
                    YT_VERIFY(row.GetHeader() == readerRows[0].GetHeader());
                    YT_LOG_INFO("Replication log row violates timestamp bound (StartReplicationTimestamp: %llx, LogRecordTimestamp: %llx)",
                        replicaSnapshot->StartReplicationTimestamp,
                        timestamp);
                    return false;
                }

                if (currentRowIndex != rowIndex) {
                    THROW_ERROR_EXCEPTION("Replication log row index mismatch in tablet %v: expected %v, got %v",
                        tabletSnapshot->TabletId,
                        currentRowIndex,
                        rowIndex)
                        << HardErrorAttribute;
                }

                if (timestamp != prevTimestamp) {
                    acquireThrottler();

                    if (rowCount >= mountConfig->MaxRowsPerReplicationCommit ||
                        dataWeight >= mountConfig->MaxDataWeightPerReplicationCommit ||
                        timestampCount >= mountConfig->MaxTimestampsPerReplicationCommit ||
                        isThrottlerOverdraft())
                    {
                        tooMuch = true;
                        break;
                    }

                    ++timestampCount;
                }

                ++currentRowIndex;
                ++rowCount;

                auto rowDataWeight = GetDataWeight(row);
                dataWeight += rowDataWeight;
                dataWeightToThrottle += rowDataWeight;
                replicationRows->push_back({modificationType, replicationRow, TLockMask()});
                prevTimestamp = timestamp;
            }
        }
        acquireThrottler();

        *newReplicationRowIndex = startRowIndex + rowCount;
        *newReplicationTimestamp = prevTimestamp;
        *batchRowCount = rowCount;
        *batchDataWeight = dataWeight;

        YT_LOG_DEBUG("Finished building replication batch (StartRowIndex: %v, RowCount: %v, DataWeight: %v, "
            "NewReplicationRowIndex: %v, NewReplicationTimestamp: %llx)",
            startRowIndex,
            rowCount,
            dataWeight,
            *newReplicationRowIndex,
            *newReplicationTimestamp);

        return true;
    }


    void DoSoftBackoff(const TError& error)
    {
        YT_LOG_INFO(error, "Doing soft backoff");
        TDelayedExecutor::WaitForDuration(Config_->ReplicatorSoftBackoffTime);
    }

    void DoHardBackoff(const TError& error)
    {
        YT_LOG_INFO(error, "Doing hard backoff");
        TDelayedExecutor::WaitForDuration(Config_->ReplicatorHardBackoffTime);
    }


    i64 GetRowIndex(const TUnversionedRow& logRow)
    {
        YT_ASSERT(logRow[1].Type == EValueType::Int64);
        return logRow[1].Data.Int64;
    }

    TTimestamp GetTimestamp(const TUnversionedRow& logRow)
    {
        YT_ASSERT(logRow[2].Type == EValueType::Uint64);
        return logRow[2].Data.Uint64;
    }

    void ParseLogRow(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TTableMountConfigPtr& mountConfig,
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTypeErasedRow* replicationRow,
        ERowModificationType* modificationType,
        i64* rowIndex,
        TTimestamp* timestamp,
        bool isVersioned)
    {
        *rowIndex = GetRowIndex(logRow);
        *timestamp = GetTimestamp(logRow);
        if (TableSchema_->IsSorted()) {
            if (isVersioned) {
                ParseSortedLogRowWithTimestamps(
                    tabletSnapshot,
                    mountConfig,
                    logRow,
                    rowBuffer,
                    *timestamp,
                    replicationRow,
                    modificationType);
            } else {
                ParseSortedLogRow(
                    tabletSnapshot,
                    mountConfig,
                    logRow,
                    rowBuffer,
                    *timestamp,
                    replicationRow,
                    modificationType);
            }
        } else {
            ParseOrderedLogRow(
                tabletSnapshot,
                mountConfig,
                logRow,
                rowBuffer,
                replicationRow,
                modificationType);
        }
    }

    void ParseOrderedLogRow(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TTableMountConfigPtr& mountConfig,
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTypeErasedRow* replicationRow,
        ERowModificationType* modificationType)
    {
        int headerRows = 3;
        YT_VERIFY(logRow.GetCount() >= headerRows);

        auto mutableReplicationRow = rowBuffer->AllocateUnversioned(logRow.GetCount() - headerRows);
        int columnCount = 0;
        for (int index = headerRows; index < logRow.GetCount(); ++index) {
            if (logRow[index].Id == TabletIndexColumnId_ && !PreserveTabletIndex_) {
                continue;
            }

            int id = index - headerRows;
            mutableReplicationRow.Begin()[id] = rowBuffer->Capture(logRow[index]);
            mutableReplicationRow.Begin()[id].Id = id;
            columnCount++;
        }

        mutableReplicationRow.SetCount(columnCount);

        *modificationType = ERowModificationType::Write;
        *replicationRow = mutableReplicationRow.ToTypeErasedRow();
    }

    void ParseSortedLogRowWithTimestamps(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TTableMountConfigPtr& mountConfig,
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTimestamp timestamp,
        TTypeErasedRow* result,
        ERowModificationType* modificationType)
    {
        TVersionedRow replicationRow;

        YT_ASSERT(logRow[3].Type == EValueType::Int64);
        auto changeType = ERowModificationType(logRow[3].Data.Int64);

        int keyColumnCount = tabletSnapshot->TableSchema->GetKeyColumnCount();
        int valueColumnCount = tabletSnapshot->TableSchema->GetValueColumnCount();

        YT_ASSERT(logRow.GetCount() == keyColumnCount + valueColumnCount * 2 + 4);

        switch (changeType) {
            case ERowModificationType::Write: {
                YT_ASSERT(logRow.GetCount() >= keyColumnCount + 4);
                int replicationValueCount = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& value = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    auto flags = FromUnversionedValue<EReplicationLogDataFlags>(value);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        ++replicationValueCount;
                    }
                }
                auto mutableReplicationRow = rowBuffer->AllocateVersioned(
                    keyColumnCount,
                    replicationValueCount,
                    1,  // writeTimestampCount
                    0); // deleteTimestampCount
                for (int keyIndex = 0; keyIndex < keyColumnCount; ++keyIndex) {
                    mutableReplicationRow.BeginKeys()[keyIndex] = rowBuffer->Capture(logRow[keyIndex + 4]);
                }
                int replicationValueIndex = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& flagsValue = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    YT_ASSERT(flagsValue.Type == EValueType::Uint64);
                    auto flags = static_cast<EReplicationLogDataFlags>(flagsValue.Data.Uint64);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        TVersionedValue value;
                        static_cast<TUnversionedValue&>(value) = rowBuffer->Capture(logRow[logValueIndex * 2 + keyColumnCount + 4]);
                        value.Id = logValueIndex + keyColumnCount;
                        value.Aggregate = Any(flags & EReplicationLogDataFlags::Aggregate);
                        value.Timestamp = timestamp;
                        mutableReplicationRow.BeginValues()[replicationValueIndex++] = value;
                    }
                }
                YT_VERIFY(replicationValueIndex == replicationValueCount);
                mutableReplicationRow.BeginWriteTimestamps()[0] = timestamp;
                replicationRow = mutableReplicationRow;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating write (Row: %v)", replicationRow);
                break;
            }

            case ERowModificationType::Delete: {
                auto mutableReplicationRow = rowBuffer->AllocateVersioned(
                    keyColumnCount,
                    0,  // valueCount
                    0,  // writeTimestampCount
                    1); // deleteTimestampCount
                for (int index = 0; index < keyColumnCount; ++index) {
                    mutableReplicationRow.BeginKeys()[index] = rowBuffer->Capture(logRow[index + 4]);
                }
                mutableReplicationRow.BeginDeleteTimestamps()[0] = timestamp;
                replicationRow = mutableReplicationRow;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating delete (Row: %v)", replicationRow);
                break;
            }

            default:
                YT_ABORT();
        }

        *modificationType = ERowModificationType::VersionedWrite;
        *result = replicationRow.ToTypeErasedRow();
    }

    void ParseSortedLogRow(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TTableMountConfigPtr& mountConfig,
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTimestamp timestamp,
        TTypeErasedRow* result,
        ERowModificationType* modificationType)
    {
        TUnversionedRow replicationRow;

        YT_ASSERT(logRow[3].Type == EValueType::Int64);
        auto changeType = ERowModificationType(logRow[3].Data.Int64);

        int keyColumnCount = tabletSnapshot->TableSchema->GetKeyColumnCount();
        int valueColumnCount = tabletSnapshot->TableSchema->GetValueColumnCount();

        YT_ASSERT(logRow.GetCount() == keyColumnCount + valueColumnCount * 2 + 4);

        *modificationType = ERowModificationType::Write;

        switch (changeType) {
            case ERowModificationType::Write: {
                YT_ASSERT(logRow.GetCount() >= keyColumnCount + 4);
                int replicationValueCount = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& value = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    auto flags = FromUnversionedValue<EReplicationLogDataFlags>(value);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        ++replicationValueCount;
                    }
                }
                auto mutableReplicationRow = rowBuffer->AllocateUnversioned(
                    keyColumnCount + replicationValueCount);
                for (int keyIndex = 0; keyIndex < keyColumnCount; ++keyIndex) {
                    mutableReplicationRow.Begin()[keyIndex] = rowBuffer->Capture(logRow[keyIndex + 4]);
                    mutableReplicationRow.Begin()[keyIndex].Id = keyIndex;
                }
                int replicationValueIndex = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& flagsValue = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    YT_ASSERT(flagsValue.Type == EValueType::Uint64);
                    auto flags = static_cast<EReplicationLogDataFlags>(flagsValue.Data.Uint64);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        TUnversionedValue value;
                        static_cast<TUnversionedValue&>(value) = rowBuffer->Capture(logRow[logValueIndex * 2 + keyColumnCount + 4]);
                        value.Id = logValueIndex + keyColumnCount;
                        value.Aggregate = Any(flags & EReplicationLogDataFlags::Aggregate);
                        mutableReplicationRow.Begin()[keyColumnCount + replicationValueIndex++] = value;
                    }
                }
                YT_VERIFY(replicationValueIndex == replicationValueCount);
                replicationRow = mutableReplicationRow;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating write (Row: %v)", replicationRow);
                break;
            }

            case ERowModificationType::Delete: {
                auto mutableReplicationRow = rowBuffer->AllocateUnversioned(
                    keyColumnCount);
                for (int index = 0; index < keyColumnCount; ++index) {
                    mutableReplicationRow.Begin()[index] = rowBuffer->Capture(logRow[index + 4]);
                    mutableReplicationRow.Begin()[index].Id = index;
                }
                replicationRow = mutableReplicationRow;
                *modificationType = ERowModificationType::Delete;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating delete (Row: %v)", replicationRow);
                break;
            }

            default:
                YT_ABORT();
        }

        *result = replicationRow.ToTypeErasedRow();
    }

    static TLegacyOwningKey MakeRowBound(i64 rowIndex)
    {
        return MakeUnversionedOwningRow(
            -1, // tablet id, fake
            rowIndex);
    }
};

////////////////////////////////////////////////////////////////////////////////

TTableReplicator::TTableReplicator(
    TTabletManagerConfigPtr config,
    TTablet* tablet,
    TTableReplicaInfo* replicaInfo,
    NNative::IConnectionPtr localConnection,
    TTabletSlotPtr slot,
    ITabletSnapshotStorePtr tabletSnapshotStore,
    IHintManagerPtr hintManager,
    IInvokerPtr workerInvoker,
    IThroughputThrottlerPtr nodeInThrottler,
    IThroughputThrottlerPtr nodeOutThrottler)
    : Impl_(New<TImpl>(
        std::move(config),
        tablet,
        replicaInfo,
        std::move(localConnection),
        std::move(slot),
        std::move(tabletSnapshotStore),
        std::move(hintManager),
        std::move(workerInvoker),
        std::move(nodeInThrottler),
        std::move(nodeOutThrottler)))
{ }

TTableReplicator::~TTableReplicator() = default;

void TTableReplicator::Enable()
{
    Impl_->Enable();
}

void TTableReplicator::Disable()
{
    Impl_->Disable();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
