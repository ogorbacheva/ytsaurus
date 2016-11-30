#include "program.h"

#include <yt/build/build.h>

#include <yt/core/misc/crash_handler.h>
#include <yt/core/misc/fs.h>

#include <yt/core/logging/log_manager.h>

#include <util/system/thread.h>
#include <util/system/sigset.h>

#ifdef _unix_
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#endif
#ifdef _linux_
#include <grp.h>
#endif

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

static void PrintVersionAndExit()
{
    Cerr << GetVersion() << Endl;
    _exit(0);
}

static void PrintBuildAndExit()
{
    Cerr << "Build Time: " << GetBuildTime() << Endl;
    Cerr << "Build Host: " << GetBuildHost() << Endl;
    Cerr << "Build Machine: " << GetBuildMachine() << Endl;
    _exit(0);
}

class TProgram::TOptsParseResult
    : public NLastGetopt::TOptsParseResult
{
public:
    TOptsParseResult(TProgram* owner, int argc, const char** argv)
        : Owner_(owner)
    {
        Init(&Owner_->Opts_, argc, argv);
    }

    virtual void HandleError() const override
    {
        Owner_->OnError(CurrentExceptionMessage());
        Cerr << Endl << "Try running '" << Owner_->Argv0_ << " --help' for more information." << Endl;
        Owner_->Exit(EProgramExitCode::OptionsError);
    }

private:
    TProgram* Owner_;
};

TProgram::TProgram()
    : Opts_()
{
    Opts_.AddHelpOption();
    Opts_.AddLongOption("version", "print version and exit")
        .NoArgument()
        .Handler(&PrintVersionAndExit);
    Opts_.AddLongOption("build", "print build information and exit")
        .NoArgument()
        .Handler(&PrintBuildAndExit);
    Opts_.SetFreeArgsNum(0);
}

TProgram::~TProgram() = default;

int TProgram::Run(int argc, const char** argv)
{
    TThread::CurrentThreadSetName("ProgramMain");

    srand(time(nullptr));

    try {
        Argv0_ = Stroka(argv[0]);
        TOptsParseResult result(this, argc, argv);

        DoRun(result);
        return Exit(EProgramExitCode::OK);
    } catch (...) {
        OnError(CurrentExceptionMessage());
        return Exit(EProgramExitCode::ProgramError);
    }
}

int TProgram::Exit(EProgramExitCode code) const noexcept
{
    return Exit(static_cast<int>(code));
}

int TProgram::Exit(int code) const noexcept
{
    NLogging::TLogManager::StaticShutdown();

    // No graceful shutdown at the moment.
    _exit(code);

    // Unreachable.
    return -1;
}


void TProgram::OnError(const Stroka& message) const noexcept
{
    Cerr << message << Endl;
}

////////////////////////////////////////////////////////////////////////////////

Stroka CheckPathExistsArgMapper(const Stroka& arg)
{
    if (!NFS::Exists(arg)) {
        throw TProgramException(Format("File %v does not exist", arg));
    }
    return arg;
}

TGuid CheckGuidArgMapper(const Stroka& arg)
{
    TGuid result;
    if (!TGuid::FromString(arg, &result)) {
        throw TProgramException(Format("Error parsing guid %Qv", arg));
    }
    return result;
}

void ConfigureUids()
{
#ifdef _unix_
    uid_t ruid, euid;
#ifdef _linux_
    uid_t suid;
    YCHECK(getresuid(&ruid, &euid, &suid) == 0);
#else
    ruid = getuid();
    euid = geteuid();
#endif
    if (euid == 0) {
        YCHECK(setgroups(0, nullptr) == 0);
        // if effective uid == 0 (e. g. set-uid-root), alter saved = effective, effective = real.
#ifdef _linux_
        YCHECK(setresuid(ruid, ruid, euid) == 0);
#else
        YCHECK(setuid(euid) == 0);
        YCHECK(seteuid(ruid) == 0);
        YCHECK(setruid(ruid) == 0);
#endif
    }
    umask(0000);
#endif
}

void ConfigureSignals()
{
#ifdef _unix_
    sigset_t sigset;
    SigEmptySet(&sigset);
    SigAddSet(&sigset, SIGHUP);
    SigProcMask(SIG_BLOCK, &sigset, nullptr);
    signal(SIGPIPE, SIG_IGN);
#endif
}

void ConfigureCrashHandler()
{
    InstallCrashSignalHandler();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
