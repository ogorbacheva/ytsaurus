#include "stdafx.h"

#include "process.h"
#include "proc.h"

#include <core/misc/error.h>
#include <core/misc/fs.h>

#include <string.h>

#ifndef _win_
  #include <unistd.h>
  #include <errno.h>
  #include <sys/wait.h>
#endif

#ifdef _darwin_
  #include <crt_externs.h>
  #define environ (*_NSGetEnviron())
#endif

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

static const size_t StackSize = 4096;

////////////////////////////////////////////////////////////////////////////////

TError SafeAtomicCloseExecPipe(int pipefd[2])
{
#ifdef _linux_
    auto result = pipe2(pipefd, O_CLOEXEC);
    if (result == -1) {
        return TError("Error creating pipe")
            << TError::FromSystem();
    }
#else
#ifdef _darwin_
    {
        int result = pipe(pipefd);
        if (result == -1) {
            return TError("Error creating pipe: pipe creation failed")
                << TError::FromSystem();
        }
    }

    for (int index = 0; index < 2; ++index) {
        int getResult = ::fcntl(pipefd[index], F_GETFL);
        if (getResult == -1) {
            return TError("Error creating pipe: fcntl failed to get descriptor flags")
                << TError::FromSystem();
        }

        int setResult = ::fcntl(pipefd[index], F_SETFL, getResult | FD_CLOEXEC);
        if (setResult == -1) {
            return TError("Error creating pipe: fcntl failed to set descriptor flags")
                << TError::FromSystem();
        }
    }
#else
    return TError("Windows is not supported");
#endif
#endif
    return TError();
}

TProcess::TProcess(const Stroka& path)
    : Finished_(false)
    , Status_(0)
    , ProcessId_(-1)
    , Stack_(StackSize, 0)
{
    Pipe_[0] = Pipe_[1] = -1;
    ChildPipe_[0] = ChildPipe_[1] = -1;

    Path_.insert(Path_.end(), path.begin(), path.end());
    Path_.push_back(0);

    AddArgument(NFS::GetFileName(path));
}

TProcess::~TProcess()
{
    for (int index = 0; index < 2; ++index) {
        if (Pipe_[index] != -1) {
            ::close(Pipe_[index]);
            Pipe_[index] = -1;
        }
    }
}

void TProcess::AddArgument(const Stroka& arg)
{
    YCHECK(ProcessId_ == -1 && !Finished_);

    Args_.push_back(Copy(~arg));
}

TError TProcess::Spawn()
{
#ifdef _win_
    return TError("Windows is not supported");
#else
    YCHECK(ProcessId_ == -1 && !Finished_);

    auto error = SafeAtomicCloseExecPipe(Pipe_);
    if (!error.IsOK()) {
        return error;
    }

    // copy env
    char** iterator = environ;

    while (*iterator) {
        const char* const item = (*iterator);
        Env_.push_back(Copy(item));

        ++iterator;
    }
    Env_.push_back(nullptr);
    Args_.push_back(nullptr);

    for (int index = 0; index < 2; ++index) {
        ChildPipe_[index] = Pipe_[index];
    }

#ifdef _linux_
    int pid = ::clone(
        &TProcess::ChildMain,
        Stack_.data() + Stack_.size(),
        CLONE_VM|SIGCHLD,
        this);
#else
    int pid = vfork();
    if (pid == 0) {
        DoSpawn();
    }
#endif

    if (pid < 0) {
        return TError("Error starting child process: clone failed")
            << TErrorAttribute("path", GetPath())
            << TError::FromSystem();
    }

    YCHECK(::close(Pipe_[1]) == 0);
    Pipe_[1] = -1;

    ProcessId_ = pid;
    return TError();
#endif
}


TError TProcess::Wait()
{
#ifdef _win_
    return TError("Windows is not supported");
#else

    YCHECK(ProcessId_ != -1);
    YCHECK(Pipe_[0] != -1);
    YCHECK(Pipe_[1] == -1);

    {
        int errCode;
        if (::read(Pipe_[0], &errCode, sizeof(int)) != sizeof(int)) {
            // TODO(babenko): can't understand why we're doing this: nobody's gonna use this value anyway
            errCode = 0;
        } else {
            ::waitpid(ProcessId_, nullptr, 0);
            Finished_ = true;
            return TError("Error waiting for child process to finish: execve failed")
                << TError::FromSystem(errCode);
        }
    }

    {
        int result = ::waitpid(ProcessId_, &Status_, WUNTRACED);
        Finished_ = true;

        if (result < 0) {
            return TError::FromSystem();
        }

        YCHECK(result == ProcessId_);
    }

    return StatusToError(Status_);
#endif
}

const char* TProcess::GetPath() const
{
    return Path_.data();
}

int TProcess::GetProcessId() const
{
    return ProcessId_;
}

char* TProcess::Copy(const char* arg)
{
    size_t size = strlen(arg);
    Holder_.push_back(std::vector<char>(arg, arg + size + 1));
    return &(Holder_[Holder_.size() - 1].front());
}

int TProcess::ChildMain(void* this_)
{
    auto* process = static_cast<TProcess*>(this_);
    return process->DoSpawn();
}

int TProcess::DoSpawn()
{
    YASSERT(ChildPipe_[1] != -1);

    ::close(ChildPipe_[0]);
    ::execve(Path_.data(), Args_.data(), Env_.data());

    const int errorCode = errno;
    while (::write(ChildPipe_[1], &errorCode, sizeof(int)) < 0);

    _exit(1);
}

////////////////////////////////////////////////////////////////////////////////

} // NYT
