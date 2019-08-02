#include "host.h"

#include "config_repository.h"
#include "database.h"
#include "functions.h"
#include "http_handler.h"
#include "logger.h"
#include "runtime_components_factory.h"
#include "system_tables.h"
#include "dictionary_source.h"
#include "table_functions.h"
#include "table_functions_concat.h"
#include "tcp_handler.h"
#include "private.h"
#include "query_context.h"
#include "query_registry.h"
#include "security_manager.h"
#include "poco_config.h"
#include "config.h"
#include "storage_distributor.h"

#include <yt/ytlib/api/native/client.h>

#include <yt/client/misc/discovery.h>

#include <yt/core/concurrency/periodic_executor.h>
#include <yt/core/profiling/profile_manager.h>
#include <yt/core/misc/proc.h>
#include <yt/core/logging/log_manager.h>

#include <AggregateFunctions/registerAggregateFunctions.h>
#include <Common/CurrentMetrics.h>
#include <Common/ClickHouseRevision.h>
#include <Common/Exception.h>
#include <Common/StringUtils/StringUtils.h>
#include <Common/config.h>
#include <Common/getMultipleKeysFromConfig.h>
#include <Common/getNumberOfPhysicalCPUCores.h>
#include <Databases/DatabaseMemory.h>
#include <Dictionaries/registerDictionaries.h>
#include <Functions/registerFunctions.h>
#include <IO/HTTPCommon.h>
#include <Interpreters/AsynchronousMetrics.h>
#include <Interpreters/Context.h>
#include <Interpreters/ProcessList.h>
#include <Interpreters/IRuntimeComponentsFactory.h>
#include <server/IServer.h>
#include <Storages/System/attachSystemTables.h>
#include <Storages/StorageFactory.h>
#include <Storages/StorageMemory.h>
#include <TableFunctions/registerTableFunctions.h>
#include <Dictionaries/Embedded/GeoDictionariesLoader.h>

#include <common/DateLUT.h>
#include <common/logger_useful.h>

#include <Poco/DirectoryIterator.h>
#include <Poco/File.h>
#include <Poco/Logger.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/TCPServer.h>
#include <Poco/String.h>
#include <Poco/ThreadPool.h>
#include <Poco/Util/LayeredConfiguration.h>
#include <Poco/Util/XMLConfiguration.h>

#include <util/system/hostname.h>

#include <atomic>
#include <memory>
#include <sstream>
#include <vector>

namespace NYT::NClickHouseServer {

using namespace DB;
using namespace NProfiling;
using namespace NYTree;

static const auto& Logger = ServerLogger;

namespace {

////////////////////////////////////////////////////////////////////////////////

std::string GetCanonicalPath(std::string path)
{
    Poco::trimInPlace(path);
    if (path.empty()) {
        throw Exception("path configuration parameter is empty", DB::ErrorCodes::METRIKA_OTHER_ERROR);
    }
    if (path.back() != '/') {
        path += '/';
    }
    return path;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

class TClickHouseHost::TImpl
    : public IServer
    , public TRefCounted
{
private:
    TBootstrap* const Bootstrap_;
    const TClickHouseServerBootstrapConfigPtr Config_;
    const TString CliqueId_;
    const TString InstanceId_;
    const IInvokerPtr ControlInvoker_;
    ui16 RpcPort_;
    ui16 MonitoringPort_;
    ui16 TcpPort_;
    ui16 HttpPort_;
    TDiscoveryPtr Discovery_;

    Poco::AutoPtr<Poco::Util::LayeredConfiguration> EngineConfig_;

    Poco::AutoPtr<Poco::Channel> LogChannel;

    std::unique_ptr<DB::Context> Context;

    std::unique_ptr<DB::AsynchronousMetrics> AsynchronousMetrics;
    std::unique_ptr<DB::SessionCleaner> SessionCleaner;

    std::unique_ptr<Poco::ThreadPool> ServerPool;
    std::vector<std::unique_ptr<Poco::Net::TCPServer>> Servers;

    std::atomic<bool> Cancelled { false };

    TPeriodicExecutorPtr MemoryWatchdogExecutor_;

public:
    TImpl(
        TBootstrap* bootstrap,
        TClickHouseServerBootstrapConfigPtr config,
        std::string cliqueId,
        std::string instanceId,
        ui16 rpcPort,
        ui16 monitoringPort,
        ui16 tcpPort,
        ui16 httpPort)
        : Bootstrap_(bootstrap)
        , Config_(std::move(config))
        , CliqueId_(std::move(cliqueId))
        , InstanceId_(std::move(instanceId))
        , ControlInvoker_(Bootstrap_->GetControlInvoker())
        , RpcPort_(rpcPort)
        , MonitoringPort_(monitoringPort)
        , TcpPort_(tcpPort)
        , HttpPort_(httpPort)
    { }

    void Start()
    {
        VERIFY_INVOKER_AFFINITY(GetControlInvoker());

        MemoryWatchdogExecutor_ = New<TPeriodicExecutor>(
            ControlInvoker_,
            BIND(&TImpl::CheckMemoryUsage, MakeWeak(this)),
            Config_->MemoryWatchdog->Period);
        MemoryWatchdogExecutor_->Start();

        SetupLogger();
        EngineConfig_ = new Poco::Util::LayeredConfiguration();
        EngineConfig_->add(ConvertToPocoConfig(ConvertToNode(Config_->Engine)));
        
        Discovery_ = New<TDiscovery>(
            Config_->Discovery,
            Bootstrap_->GetRootClient(),
            ControlInvoker_,
            std::vector<TString>{"host", "rpc_port", "monitoring_port", "tcp_port", "http_port"},
            Logger);
        
        SetupContext();
        WarmupDictionaries();
        SetupHandlers();
        
        Discovery_->StartPolling();

        TDiscovery::TAttributeDictionary attributes = {
            {"host", ConvertToNode(GetFQDNHostName())},
            {"rpc_port", ConvertToNode(RpcPort_)},
            {"monitoring_port", ConvertToNode(MonitoringPort_)},
            {"tcp_port", ConvertToNode(TcpPort_)},
            {"http_port", ConvertToNode(HttpPort_)},
        };

        WaitFor(Discovery_->Enter(InstanceId_, attributes))
            .ThrowOnError();

        ProfilingExecutor_ = New<TPeriodicExecutor>(
            Bootstrap_->GetControlInvoker(),
            BIND(&TImpl::OnProfiling, MakeWeak(this)),
            Config_->ProfilingPeriod);
        ProfilingExecutor_->Start();
    }

    Poco::Logger& logger() const override
    {
        return Poco::Logger::root();
    }

    Poco::Util::LayeredConfiguration& config() const override
    {
        return *const_cast<Poco::Util::LayeredConfiguration*>(EngineConfig_.get());
    }

    DB::Context& context() const override
    {
        return *Context;
    }

    bool isCancelled() const override
    {
        return Cancelled;
    }

    TClusterNodes GetNodes() const
    {
        auto nodeList = Discovery_->List();
        TClusterNodes result;
        result.reserve(nodeList.size());
        for (const auto& [_, attributes] : nodeList) {
            auto host = attributes.at("host")->AsString()->GetValue();
            auto tcpPort = attributes.at("tcp_port")->AsUint64()->GetValue();
            result.push_back(CreateClusterNode(TClusterNodeName{host, tcpPort}, Context->getSettingsRef(), tcpPort));
        }
        return result;
    }

    void OnProfiling()
    {
        VERIFY_INVOKER_AFFINITY(ControlInvoker_);

        YT_LOG_DEBUG("Flushing profiling");

        Bootstrap_->GetQueryRegistry()->OnProfiling();

        for (int index = 0; index < static_cast<int>(CurrentMetrics::end()); ++index) {
            const auto* name = CurrentMetrics::getName(index);
            auto value = CurrentMetrics::values[index].load();
            ServerProfiler.Enqueue(
                "/ch_metrics/" + CamelCaseToUnderscoreCase(TString(name)),
                value,
                EMetricType::Gauge);
        }

        YT_LOG_DEBUG("Profiling flushed");
    }

    const IInvokerPtr& GetControlInvoker() const
    {
        return ControlInvoker_;
    }

private:
    TPeriodicExecutorPtr ProfilingExecutor_;

    void SetupLogger()
    {
        LogChannel = CreateLogChannel(EngineLogger);

        auto& rootLogger = Poco::Logger::root();
        rootLogger.close();
        rootLogger.setChannel(LogChannel);
        rootLogger.setLevel(Config_->Engine->LogLevel);
    }

    void SetupContext()
    {
        YT_LOG_INFO("Setting up context");

        auto storageHomePath = Config_->Engine->CypressRootPath;

        auto securityManager = CreateUsersManager(Bootstrap_, CliqueId_);
        auto dictionariesConfigRepository = CreateDictionaryConfigRepository(Config_->Engine->Dictionaries);
        auto geoDictionariesLoader = std::make_unique<GeoDictionariesLoader>();
        auto runtimeComponentsFactory = CreateRuntimeComponentsFactory(
            std::move(securityManager),
            std::move(dictionariesConfigRepository),
            std::move(geoDictionariesLoader));

        Context = std::make_unique<DB::Context>(Context::createGlobal(std::move(runtimeComponentsFactory)));
        Context->setGlobalContext(*Context);
        Context->setApplicationType(Context::ApplicationType::SERVER);

        Context->setConfig(EngineConfig_);

        Context->setUsersConfig(ConvertToPocoConfig(ConvertToNode(Config_->Engine->Users)));

        registerFunctions();
        registerAggregateFunctions();
        registerTableFunctions();
        registerStorageMemory(StorageFactory::instance());
        registerDictionaries();

        RegisterFunctions();
        RegisterTableFunctions();
        RegisterConcatenatingTableFunctions();
        RegisterTableDictionarySource(Bootstrap_);
        RegisterStorageDistributor();

        CurrentMetrics::set(CurrentMetrics::Revision, ClickHouseRevision::get());
        CurrentMetrics::set(CurrentMetrics::VersionInteger, ClickHouseRevision::getVersionInteger());

        // Initialize DateLUT early, to not interfere with running time of first query.
        YT_LOG_INFO("Initializing DateLUT");
        DateLUT::instance();
        YT_LOG_INFO("DateLUT initialized (TimeZone: %v)", DateLUT::instance().getTimeZone());

        // Limit on total number of concurrently executed queries.
        Context->getProcessList().setMaxSize(EngineConfig_->getInt("max_concurrent_queries", 0));

        // Size of cache for uncompressed blocks. Zero means disabled.
        size_t uncompressedCacheSize = EngineConfig_->getUInt64("uncompressed_cache_size", 0);
        if (uncompressedCacheSize) {
            Context->setUncompressedCache(uncompressedCacheSize);
        }

        Context->setDefaultProfiles(*EngineConfig_);

        std::string path = GetCanonicalPath(Config_->Engine->DataPath);
        Poco::File(path).createDirectories();
        Context->setPath(path);

        // Directory with temporary data for processing of hard queries.
        {
            // TODO(max42): tmpfs here?
            std::string tmpPath = EngineConfig_->getString("tmp_path", path + "tmp/");
            Poco::File(tmpPath).createDirectories();
            Context->setTemporaryPath(tmpPath);

            // Clearing old temporary files.
            for (Poco::DirectoryIterator it(tmpPath), end; it != end; ++it) {
                if (it->isFile() && startsWith(it.name(), "tmp")) {
                    YT_LOG_DEBUG("Removing old temporary file (Path: %v)", it->path());
                    it->remove();
                }
            }
        }

#if defined(COLLECT_ASYNCHRONUS_METRICS)
        // This object will periodically calculate some metrics.
        AsynchronousMetrics.reset(new DB::AsynchronousMetrics(*Context));
#endif

        // This object will periodically cleanup sessions.
        SessionCleaner.reset(new DB::SessionCleaner(*Context));

        Context->initializeSystemLogs();

        // Database for system tables.
        {
            auto systemDatabase = std::make_shared<DatabaseMemory>("system");

            AttachSystemTables(*systemDatabase, Discovery_);

            if (AsynchronousMetrics) {
                attachSystemTablesAsync(*systemDatabase, *AsynchronousMetrics);
            }

            Context->addDatabase("system", systemDatabase);
        }

        // Default database that wraps connection to YT cluster.
        {
            auto defaultDatabase = CreateDatabase();
            Context->addDatabase("default", defaultDatabase);
            Context->addDatabase(CliqueId_, defaultDatabase);
        }

        std::string defaultDatabase = EngineConfig_->getString("default_database", "default");
        Context->setCurrentDatabase(defaultDatabase);
    }

    void WarmupDictionaries()
    {
        Context->getEmbeddedDictionaries();
        Context->getExternalDictionaries();
    }

    void SetupHandlers()
    {
        YT_LOG_INFO("Setting up handlers");

        const auto& settings = Context->getSettingsRef();

        ServerPool = std::make_unique<Poco::ThreadPool>(3, EngineConfig_->getInt("max_connections", 1024));

        auto listenHosts = Config_->Engine->ListenHosts;

        bool tryListen = false;
        if (listenHosts.empty()) {
            listenHosts.emplace_back("::1");
            listenHosts.emplace_back("127.0.0.1");
            tryListen = true;
        }

        auto makeSocketAddress = [&] (const std::string& host, UInt16 port) {
            Poco::Net::SocketAddress socketAddress;
            try {
                socketAddress = Poco::Net::SocketAddress(host, port);
            } catch (const Poco::Net::DNSException& e) {
                if (e.code() == EAI_FAMILY
#if defined(EAI_ADDRFAMILY)
                    || e.code() == EAI_ADDRFAMILY
#endif
                    )
                {
                    YT_LOG_ERROR("Cannot resolve listen_host (Host: %v, Error: %v)", host, e.message());
                }

                throw;
            }
            return socketAddress;
        };

        for (const auto& listenHost: listenHosts) {
            try {
                // HTTP
                {
                    auto socketAddress = makeSocketAddress(listenHost, HttpPort_);

                    Poco::Net::ServerSocket socket(socketAddress);
                    socket.setReceiveTimeout(settings.receive_timeout);
                    socket.setSendTimeout(settings.send_timeout);

                    Poco::Timespan keepAliveTimeout(EngineConfig_->getInt("keep_alive_timeout", 10), 0);

                    Poco::Net::HTTPServerParams::Ptr httpParams = new Poco::Net::HTTPServerParams();
                    httpParams->setTimeout(settings.receive_timeout);
                    httpParams->setKeepAliveTimeout(keepAliveTimeout);

                    Servers.emplace_back(new Poco::Net::HTTPServer(
                        CreateHttpHandlerFactory(Bootstrap_, *this),
                        *ServerPool,
                        socket,
                        httpParams));
                }

                // TCP
                {
                    auto socketAddress = makeSocketAddress(listenHost, TcpPort_);

                    Poco::Net::ServerSocket socket(socketAddress);
                    socket.setReceiveTimeout(settings.receive_timeout);
                    socket.setSendTimeout(settings.send_timeout);

                    Servers.emplace_back(new Poco::Net::TCPServer(
                        CreateTcpHandlerFactory(Bootstrap_, *this),
                        *ServerPool,
                        socket,
                        new Poco::Net::TCPServerParams()));
                }
            } catch (const Poco::Net::NetException& e) {
                if (!(tryListen && e.code() == POCO_EPROTONOSUPPORT)) {
                    throw;
                }

                YT_LOG_ERROR("Error setting up listenHost (ListenHost: %v, What: %v, Error: %v)", listenHost, e.what(), e.message());
            }
        }

        for (auto& server: Servers) {
            server->start();
        }

        YT_LOG_INFO("Handlers set up");
    }

    void CheckMemoryUsage()
    {
        auto usage = GetProcessMemoryUsage();
        auto total = usage.Rss + usage.Shared;
        YT_LOG_INFO(
            "Checking memory usage "
            "(Rss: %v, Shared: %v, Total: %v, MemoryLimit: %v, CodicilWatermark: %v)",
            usage.Rss,
            usage.Shared,
            total,
            Config_->MemoryWatchdog->MemoryLimit,
            Config_->MemoryWatchdog->CodicilWatermark);
        if (total + Config_->MemoryWatchdog->CodicilWatermark > Config_->MemoryWatchdog->MemoryLimit) {
            YT_LOG_ERROR("We are close to OOM, printing query digest codicils and killing ourselves");
            NYT::NLogging::TLogManager::Get()->Shutdown();
            Bootstrap_->GetQueryRegistry()->DumpCodicils();
            _exit(MemoryLimitExceededExitCode);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TClickHouseHost::TClickHouseHost(
    TBootstrap* bootstrap,
    TClickHouseServerBootstrapConfigPtr config,
    std::string cliqueId,
    std::string instanceId,
    ui16 rpcPort,
    ui16 monitoringPort,
    ui16 tcpPort,
    ui16 httpPort)
    : Impl_(New<TImpl>(
        bootstrap,
        std::move(config),
        std::move(cliqueId),
        std::move(instanceId),
        rpcPort,
        monitoringPort,
        tcpPort,
        httpPort))
{ }

void TClickHouseHost::Start()
{
    Impl_->Start();
}

const IInvokerPtr& TClickHouseHost::GetControlInvoker() const
{
    return Impl_->GetControlInvoker();
}

DB::Context& TClickHouseHost::GetContext() const
{
    return Impl_->context();
}

TClusterNodes TClickHouseHost::GetNodes() const
{
    return Impl_->GetNodes();
}

TClickHouseHost::~TClickHouseHost() = default;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
