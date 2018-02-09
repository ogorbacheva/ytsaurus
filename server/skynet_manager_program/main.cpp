#include <yt/server/skynet_manager/bootstrap.h>
#include <yt/server/skynet_manager/config.h>

#include <yt/ytlib/program/program.h>
#include <yt/ytlib/program/program_config_mixin.h>
#include <yt/ytlib/program/program_pdeathsig_mixin.h>
#include <yt/ytlib/program/configure_singletons.h>

namespace NYT {

using namespace NSkynetManager;

////////////////////////////////////////////////////////////////////////////////

class TSkynetManagerProgram
    : public TProgram
    , public TProgramPdeathsigMixin
    , public TProgramConfigMixin<TSkynetManagerConfig>
{
public:
    TSkynetManagerProgram()
        : TProgramPdeathsigMixin(Opts_)
        , TProgramConfigMixin(Opts_, false)
    { }

protected:
    virtual void DoRun(const NLastGetopt::TOptsParseResult& parseResult) override
    {
        TThread::CurrentThreadSetName("SkynetManager");

        ConfigureUids();
        ConfigureSignals();
        ConfigureCrashHandler();
        ConfigureExitZeroOnSigterm();

        if (HandlePdeathsigOptions()) {
            return;
        }

        if (HandleConfigOptions()) {
            return;
        }

        auto config = GetConfig();
        for (auto cluster : config->Clusters) {
            cluster->LoadToken();
        }

        ConfigureSingletons(config);

        auto bootstrap = New<TBootstrap>(std::move(config));
        bootstrap->Start();
        Sleep(TDuration::Max());
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

int main(int argc, const char** argv)
{
    return NYT::TSkynetManagerProgram().Run(argc, argv);
}

