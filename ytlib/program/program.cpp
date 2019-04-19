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
#include <sys/prctl.h>
#endif

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

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
{
    Opts_.AddHelpOption();
    Opts_.AddLongOption("version", "print version and exit")
        .NoArgument()
        .Handler0(std::bind(&TProgram::PrintVersionAndExit, this));
    Opts_.AddLongOption("build", "print build information and exit")
        .NoArgument()
        .Handler0(std::bind(&TProgram::PrintBuildAndExit, this));
    Opts_.SetFreeArgsNum(0);
}

void TProgram::SetCrashOnError()
{
    CrashOnError_ = true;
}

TProgram::~TProgram() = default;

int TProgram::Run(int argc, const char** argv)
{
    TThread::CurrentThreadSetName("ProgramMain");

    srand(time(nullptr));

    auto run = [&] {
        Argv0_ = TString(argv[0]);
        TOptsParseResult result(this, argc, argv);

        DoRun(result);
    };

    if (!CrashOnError_) {
        try {
            run();
            return Exit(EProgramExitCode::OK);
        } catch (...) {
            OnError(CurrentExceptionMessage());
            return Exit(EProgramExitCode::ProgramError);
        }
    } else {
        run();
        return Exit(EProgramExitCode::OK);
    }
}

int TProgram::Exit(EProgramExitCode code) const noexcept
{
    Exit(static_cast<int>(code));
}

int TProgram::Exit(int code) const noexcept
{
    NLogging::TLogManager::StaticShutdown();

    // No graceful shutdown at the moment.
    _exit(code);

    Y_UNREACHABLE();
}

void TProgram::OnError(const TString& message) const noexcept
{
    try {
        Cerr << message << Endl;
    } catch (...) {
        // Just ignore it; STDERR might be closed already,
        // and write() would result in EPIPE.
    }
}

void TProgram::PrintVersionAndExit()
{
    Cout << GetVersion() << Endl;
    _exit(0);
}

void TProgram::PrintBuildAndExit()
{
    Cout << "Build Time: " << GetBuildTime() << Endl;
    Cout << "Build Host: " << GetBuildHost() << Endl;
    _exit(0);
}

////////////////////////////////////////////////////////////////////////////////

TProgramException::TProgramException(TString what)
    : What_(std::move(what))
{ }

const char* TProgramException::what() const noexcept
{
    return What_.c_str();
}

////////////////////////////////////////////////////////////////////////////////

TString CheckPathExistsArgMapper(const TString& arg)
{
    if (!NFS::Exists(arg)) {
        throw TProgramException(Format("File %v does not exist", arg));
    }
    return arg;
}

TGuid CheckGuidArgMapper(const TString& arg)
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
        // Make server suid_dumpable = 1.
        YCHECK(prctl(PR_SET_DUMPABLE, 1)  == 0);
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

void ExitZero(int /* unused */)
{
    _exit(0);
}

void ConfigureExitZeroOnSigterm()
{
#ifdef _unix_
    signal(SIGTERM, ExitZero);
#endif
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
