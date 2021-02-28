#pragma once

#include "public.h"

#include <yt/server/node/exec_agent/public.h>

#include <yt/server/node/data_node/public.h>

#include <yt/server/node/job_agent/public.h>

#include <yt/server/node/query_agent/public.h>

#include <yt/server/node/tablet_node/public.h>

#include <yt/server/lib/containers/public.h>

#include <yt/server/lib/job_proxy/public.h>

#include <yt/ytlib/api/native/public.h>

#include <yt/ytlib/chunk_client/public.h>

#include <yt/ytlib/misc/public.h>

#include <yt/client/node_tracker_client/node_directory.h>

#include <yt/ytlib/query_client/public.h>

#include <yt/ytlib/monitoring/public.h>

#include <yt/ytlib/object_client/public.h>

#include <yt/core/bus/public.h>

#include <yt/core/http/public.h>

#include <yt/core/concurrency/action_queue.h>
#include <yt/core/concurrency/throughput_throttler.h>
#include <yt/core/concurrency/two_level_fair_share_thread_pool.h>

#include <yt/core/net/address.h>

#include <yt/core/rpc/public.h>

#include <yt/core/ytree/public.h>

namespace NYT::NClusterNode {

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
{
public:
    TBootstrap(TClusterNodeConfigPtr config, NYTree::INodePtr configNode);
    ~TBootstrap();

    const TClusterNodeConfigPtr& GetConfig() const;
    const IInvokerPtr& GetControlInvoker() const;
    const IInvokerPtr& GetJobInvoker() const;
    IInvokerPtr GetQueryPoolInvoker(
        const TString& poolName,
        double weight,
        const NConcurrency::TFairShareThreadPoolTag& tag) const;
    const IInvokerPtr& GetTabletLookupPoolInvoker() const;
    const IInvokerPtr& GetTableReplicatorPoolInvoker() const;
    const IInvokerPtr& GetTransactionTrackerInvoker() const;
    const IPrioritizedInvokerPtr& GetStorageHeavyInvoker() const;
    const IInvokerPtr& GetStorageLightInvoker() const;
    IInvokerPtr GetStorageLookupInvoker() const;
    const IInvokerPtr& GetJobThrottlerInvoker() const;
    const NApi::NNative::IClientPtr& GetMasterClient() const;
    const NApi::NNative::IConnectionPtr& GetMasterConnection() const;
    const NRpc::IServerPtr& GetRpcServer() const;
    const NYTree::IMapNodePtr& GetOrchidRoot() const;
    const NJobAgent::TJobControllerPtr& GetJobController() const;
    const NJobAgent::TJobReporterPtr& GetJobReporter() const;
    const NTabletNode::IHintManagerPtr& GetTabletNodeHintManager() const;
    const NTabletNode::ISlotManagerPtr& GetTabletSlotManager() const;
    const NTabletNode::TSecurityManagerPtr& GetSecurityManager() const;
    const NTabletNode::IInMemoryManagerPtr& GetInMemoryManager() const;
    const NTabletNode::TVersionedChunkMetaManagerPtr& GetVersionedChunkMetaManager() const;
    const NTabletNode::IStructuredLoggerPtr& GetTabletNodeStructuredLogger() const;
    const NExecAgent::TSlotManagerPtr& GetExecSlotManager() const;
    const NJobAgent::TGpuManagerPtr& GetGpuManager() const;
    const TNodeMemoryTrackerPtr& GetMemoryUsageTracker() const;
    const NDataNode::TChunkStorePtr& GetChunkStore() const;
    const NDataNode::TChunkCachePtr& GetChunkCache() const;
    const NDataNode::TChunkRegistryPtr& GetChunkRegistry() const;
    const NDataNode::TSessionManagerPtr& GetSessionManager() const;
    const NDataNode::IChunkMetaManagerPtr& GetChunkMetaManager() const;
    const NDataNode::TChunkBlockManagerPtr& GetChunkBlockManager() const;
    NDataNode::TNetworkStatistics& GetNetworkStatistics() const;
    const NChunkClient::IBlockCachePtr& GetBlockCache() const;
    const NDataNode::TP2PBlockDistributorPtr& GetP2PBlockDistributor() const;
    const NDataNode::TBlockPeerTablePtr& GetBlockPeerTable() const;
    const NDataNode::TBlockPeerUpdaterPtr& GetBlockPeerUpdater() const;
    const NDataNode::IBlobReaderCachePtr& GetBlobReaderCache() const;
    const NDataNode::TTableSchemaCachePtr& GetTableSchemaCache() const;
    const NDataNode::IJournalDispatcherPtr& GetJournalDispatcher() const;
    const NDataNode::TMasterConnectorPtr& GetMasterConnector() const;
    const NQueryClient::IColumnEvaluatorCachePtr& GetColumnEvaluatorCache() const;
    const NNodeTrackerClient::TNodeDirectoryPtr& GetNodeDirectory() const;
    const TClusterNodeDynamicConfigManagerPtr& GetDynamicConfigManager() const;
    const TNodeResourceManagerPtr& GetNodeResourceManager() const;

    const NConcurrency::IThroughputThrottlerPtr& GetDataNodeThrottler(NDataNode::EDataNodeThrottlerKind kind) const;
    const NConcurrency::IThroughputThrottlerPtr& GetTabletNodeThrottler(NTabletNode::ETabletNodeThrottlerKind kind) const;
    const NConcurrency::IThroughputThrottlerPtr& GetDataNodeInThrottler(const TWorkloadDescriptor& descriptor) const;
    const NConcurrency::IThroughputThrottlerPtr& GetDataNodeOutThrottler(const TWorkloadDescriptor& descriptor) const;
    const NConcurrency::IThroughputThrottlerPtr& GetTabletNodeInThrottler(EWorkloadCategory category) const;
    const NConcurrency::IThroughputThrottlerPtr& GetTabletNodeOutThrottler(EWorkloadCategory category) const;

    NObjectClient::TCellId GetCellId() const;
    NObjectClient::TCellId GetCellId(NObjectClient::TCellTag cellTag) const;
    std::vector<TString> GetMasterAddressesOrThrow(NObjectClient::TCellTag cellTag) const;
    NNodeTrackerClient::TNetworkPreferenceList GetLocalNetworks() const;
    std::optional<TString> GetDefaultNetworkName() const;
    TString GetDefaultLocalAddressOrThrow() const;
    NExecAgent::EJobEnvironmentType GetEnvironmentType() const;
    bool IsSimpleEnvironment() const;

    NJobProxy::TJobProxyConfigPtr BuildJobProxyConfig() const;

    void Initialize();
    void Run();
    void ValidateSnapshot(const TString& fileName);

    bool IsReadOnly() const;

private:
    const TClusterNodeConfigPtr Config_;
    const NYTree::INodePtr ConfigNode_;

    NConcurrency::TActionQueuePtr ControlActionQueue_;
    NConcurrency::TActionQueuePtr JobActionQueue_;
    NConcurrency::ITwoLevelFairShareThreadPoolPtr QueryThreadPool_;
    NConcurrency::TThreadPoolPtr TabletLookupThreadPool_;
    NConcurrency::TThreadPoolPtr TableReplicatorThreadPool_;
    NConcurrency::TActionQueuePtr TransactionTrackerQueue_;
    NConcurrency::TThreadPoolPtr StorageHeavyThreadPool_;
    IPrioritizedInvokerPtr StorageHeavyInvoker_;
    NConcurrency::TThreadPoolPtr StorageLightThreadPool_;
    NConcurrency::IFairShareThreadPoolPtr StorageLookupThreadPool_;
    NConcurrency::TActionQueuePtr MasterCacheQueue_;

    NMonitoring::TMonitoringManagerPtr MonitoringManager_;
    NBus::IBusServerPtr BusServer_;
    NApi::NNative::IConnectionPtr MasterConnection_;
    NApi::NNative::IClientPtr MasterClient_;
    NRpc::IServerPtr RpcServer_;
    std::vector<NObjectClient::ICachingObjectServicePtr> CachingObjectServices_;
    NHttp::IServerPtr HttpServer_;
    NHttp::IServerPtr SkynetHttpServer_;
    NYTree::IMapNodePtr OrchidRoot_;
    NJobAgent::TJobControllerPtr JobController_;
    NJobAgent::TJobReporterPtr JobReporter_;
    NExecAgent::TSlotManagerPtr ExecSlotManager_;
    NJobAgent::TGpuManagerPtr GpuManager_;
    NJobProxy::TJobProxyConfigPtr JobProxyConfigTemplate_;
    TNodeMemoryTrackerPtr MemoryUsageTracker_;
    NExecAgent::TSchedulerConnectorPtr SchedulerConnector_;
    NDataNode::TChunkStorePtr ChunkStore_;
    NDataNode::TChunkCachePtr ChunkCache_;
    NDataNode::TChunkRegistryPtr ChunkRegistry_;
    NDataNode::TSessionManagerPtr SessionManager_;
    NDataNode::IChunkMetaManagerPtr ChunkMetaManager_;
    NDataNode::TChunkBlockManagerPtr ChunkBlockManager_;
    std::unique_ptr<NDataNode::TNetworkStatistics> NetworkStatistics_;
    NChunkClient::IClientBlockCachePtr ClientBlockCache_;
    NChunkClient::IBlockCachePtr BlockCache_;
    NDataNode::TBlockPeerTablePtr BlockPeerTable_;
    NDataNode::TBlockPeerUpdaterPtr BlockPeerUpdater_;
    NDataNode::TP2PBlockDistributorPtr P2PBlockDistributor_;
    NDataNode::IBlobReaderCachePtr BlobReaderCache_;
    NDataNode::TTableSchemaCachePtr TableSchemaCache_;
    NDataNode::IJournalDispatcherPtr JournalDispatcher_;
    NDataNode::TMasterConnectorPtr MasterConnector_;
    ICoreDumperPtr CoreDumper_;
    TClusterNodeDynamicConfigManagerPtr DynamicConfigManager_;
    NObjectClient::TObjectServiceCachePtr ObjectServiceCache_;

    TEnumIndexedVector<NDataNode::EDataNodeThrottlerKind, NConcurrency::IReconfigurableThroughputThrottlerPtr> RawDataNodeThrottlers_;
    TEnumIndexedVector<NDataNode::EDataNodeThrottlerKind, NConcurrency::IThroughputThrottlerPtr> DataNodeThrottlers_;
    TEnumIndexedVector<NTabletNode::ETabletNodeThrottlerKind, NConcurrency::IReconfigurableThroughputThrottlerPtr> RawTabletNodeThrottlers_;
    TEnumIndexedVector<NTabletNode::ETabletNodeThrottlerKind, NConcurrency::IThroughputThrottlerPtr> TabletNodeThrottlers_;
    NConcurrency::IThroughputThrottlerPtr TabletNodePreloadInThrottler_;

    NTabletNode::IHintManagerPtr TabletNodeHintManager_;
    NTabletNode::ISlotManagerPtr TabletSlotManager_;
    NTabletNode::TSecurityManagerPtr SecurityManager_;
    NTabletNode::IInMemoryManagerPtr InMemoryManager_;
    NTabletNode::TVersionedChunkMetaManagerPtr VersionedChunkMetaManager_;
    NTabletNode::IStructuredLoggerPtr TabletNodeStructuredLogger_;

    NQueryClient::IColumnEvaluatorCachePtr ColumnEvaluatorCache_;

#ifdef __linux__
    NContainers::TInstanceLimitsTrackerPtr InstanceLimitsTracker_;
#endif

    TNodeResourceManagerPtr NodeResourceManager_;

    NTabletNode::IStoreCompactorPtr StoreCompactor_;
    NTabletNode::IStoreFlusherPtr StoreFlusher_;
    NTabletNode::IStoreTrimmerPtr StoreTrimmer_;
    NTabletNode::IPartitionBalancerPtr PartitionBalancer_;
    NTabletNode::IBackingStoreCleanerPtr BackingStoreCleaner_;

    void DoInitialize();
    void DoRun();
    void DoValidateConfig();
    void DoValidateSnapshot(const TString& fileName);
    void PopulateAlerts(std::vector<TError>* alerts);

    void OnMasterConnected();
    void OnMasterDisconnected();

    void OnDynamicConfigChanged(
        const TClusterNodeDynamicConfigPtr& oldConfig,
        const TClusterNodeDynamicConfigPtr& newConfig);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
