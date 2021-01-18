#include <yt/server/node/cluster_node/bootstrap.h>
#include <yt/server/node/cluster_node/config.h>

#include <yt/ytlib/program/program.h>
#include <yt/ytlib/program/program_config_mixin.h>
#include <yt/ytlib/program/program_tool_mixin.h>
#include <yt/ytlib/program/program_pdeathsig_mixin.h>
#include <yt/ytlib/program/program_setsid_mixin.h>
#include <yt/ytlib/program/helpers.h>

#include <yt/library/phdr_cache/phdr_cache.h>

#include <library/cpp/ytalloc/api/ytalloc.h>

#include <yt/core/bus/tcp/dispatcher.h>

#include <yt/core/misc/ref_counted_tracker_profiler.h>

#include <yt/core/ytalloc/bindings.h>

namespace NYT::NClusterNode {

////////////////////////////////////////////////////////////////////////////////

class TClusterNodeProgram
    : public TProgram
    , public TProgramPdeathsigMixin
    , public TProgramSetsidMixin
    , public TProgramToolMixin
    , public TProgramConfigMixin<NClusterNode::TClusterNodeConfig>
{
public:
    TClusterNodeProgram()
        : TProgramPdeathsigMixin(Opts_)
        , TProgramSetsidMixin(Opts_)
        , TProgramToolMixin(Opts_)
        , TProgramConfigMixin(Opts_, false)
    {
        Opts_
            .AddLongOption("validate-snapshot")
            .StoreMappedResult(&ValidateSnapshot_, &CheckPathExistsArgMapper)
            .RequiredArgument("SNAPSHOT");
        Opts_
            .AddLongOption("sleep-after-initialize", "sleep for 10s after calling TBootstrap::Initialize()")
            .SetFlag(&SleepAfterInitialize_)
            .NoArgument();
    }

protected:
    virtual void DoRun(const NLastGetopt::TOptsParseResult& parseResult) override
    {
        TThread::SetCurrentThreadName("NodeMain");

        ConfigureUids();
        ConfigureIgnoreSigpipe();
        ConfigureCrashHandler();
        ConfigureExitZeroOnSigterm();
        EnablePhdrCache();
        EnableRefCountedTrackerProfiling();
        NYTAlloc::EnableYTLogging();
        NYTAlloc::EnableYTProfiling();
        NYTAlloc::InitializeLibunwindInterop();
        NYTAlloc::SetEnableEagerMemoryRelease(false);
        NYTAlloc::EnableStockpile();
        NYTAlloc::MlockFileMappings();

        if (HandleSetsidOptions()) {
            return;
        }
        if (HandlePdeathsigOptions()) {
            return;
        }

        if (HandleToolOptions()) {
            return;
        }

        if (HandleConfigOptions()) {
            return;
        }

        auto config = GetConfig();
        auto configNode = GetConfigNode();

        if (ValidateSnapshot_) {
            NBus::TTcpDispatcher::Get()->DisableNetworking();

            config->ClusterConnection->EnableNetworking = false;

            config->Logging = NLogging::TLogManagerConfig::CreateQuiet();
        }

        ConfigureSingletons(config);
        StartDiagnosticDump(config);

        // TODO(babenko): This memory leak is intentional.
        // We should avoid destroying bootstrap since some of the subsystems
        // may be holding a reference to it and continue running some actions in background threads.
        auto* bootstrap = new NClusterNode::TBootstrap(std::move(config), std::move(configNode));
        bootstrap->Initialize();

        if (SleepAfterInitialize_) {
            NConcurrency::TDelayedExecutor::WaitForDuration(TDuration::Seconds(10));
        }

        if (ValidateSnapshot_) {
            bootstrap->ValidateSnapshot(ValidateSnapshot_);
        } else {
            bootstrap->Run();
        }
    }

private:
    TString ValidateSnapshot_;
    bool SleepAfterInitialize_ = false;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClusterNode
