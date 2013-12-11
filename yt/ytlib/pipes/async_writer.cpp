#include "async_writer.h"

#include "io_dispatcher.h"
#include "private.h"

namespace NYT {
namespace NPipes {

static const size_t WriteBufferSize = 64 * 1024;

TAsyncWriter::TAsyncWriter(int fd)
    : FD(fd)
    , BytesWrittenTotal(0)
    , NeedToClose(false)
    , Closed(false)
    , LastSystemError(0)
    , Logger(WriterLogger)
{
    Logger.AddTag(Sprintf("FD: %s", ~ToString(fd)));

    FDWatcher.set(fd, ev::WRITE);

    RegistrationError = TIODispatcher::Get()->AsyncRegister(this);
}

TAsyncWriter::~TAsyncWriter()
{
    Close();
}

void TAsyncWriter::Start(ev::dynamic_loop& eventLoop)
{
    VERIFY_THREAD_AFFINITY(EventLoop);

    TGuard<TSpinLock> guard(WriteLock);

    StartWatcher.set(eventLoop);
    StartWatcher.set<TAsyncWriter, &TAsyncWriter::OnStart>(this);
    StartWatcher.start();

    FDWatcher.set(eventLoop);
    FDWatcher.set<TAsyncWriter, &TAsyncWriter::OnWrite>(this);
    FDWatcher.start();
}

void TAsyncWriter::OnStart(ev::async&, int eventType)
{
    VERIFY_THREAD_AFFINITY(EventLoop);
    YCHECK(eventType == ev::ASYNC);

    FDWatcher.start();
}

void TAsyncWriter::OnWrite(ev::io&, int eventType)
{
    VERIFY_THREAD_AFFINITY(EventLoop);
    YCHECK(eventType == ev::WRITE);

    TGuard<TSpinLock> guard(WriteLock);

    if (WriteBuffer.Size() != 0 || NeedToClose) {
        YCHECK(WriteBuffer.Size() >= BytesWrittenTotal);
        const size_t size = WriteBuffer.Size() - BytesWrittenTotal;
        const char* data = WriteBuffer.Begin() + BytesWrittenTotal;

        const size_t bytesWritten = TryWrite(data, size);

        if (LastSystemError == 0) {
            BytesWrittenTotal += bytesWritten;
            TryCleanBuffer();
            if (NeedToClose && WriteBuffer.Size() == 0) {
                Close();
            }
        } else {
            // Error.  We've done all we could
            Close();
        }

        if (ReadyPromise.HasValue()) {
            if (LastSystemError == 0) {
                ReadyPromise->Set(TError());
            } else {
                ReadyPromise->Set(TError::FromSystem(LastSystemError));
            }
            ReadyPromise.Reset();
        }
    } else {
        // I stop because these is nothing to write
        FDWatcher.stop();
    }
}

bool TAsyncWriter::Write(const void* data, size_t size)
{
    VERIFY_THREAD_AFFINITY_ANY();
    YCHECK(!NeedToClose);

    TGuard<TSpinLock> guard(WriteLock);

    size_t bytesWritten = 0;

    if (WriteBuffer.Size() == 0) {
        LOG_DEBUG("Internal buffer is empty. Trying to write %" PRISZT " bytes", size);
        bytesWritten = TryWrite(static_cast<const char*>(data), size);
    }

    YCHECK(!ReadyPromise.HasValue());

    LOG_DEBUG("%" PRISZT " bytes has been added to internal write buffer", size - bytesWritten);
    WriteBuffer.Append(data + bytesWritten, size - bytesWritten);

    // restart watcher
    if (LastSystemError == 0) {
        if (WriteBuffer.Size() > 0 || NeedToClose) {
            StartWatcher.send();
        }
    } else {
        YCHECK(Closed);
    }

    return ((LastSystemError != 0) || (WriteBuffer.Size() >= WriteBufferSize));
}

size_t TAsyncWriter::TryWrite(const char* data, size_t size)
{
    int errCode;
    do {
        errCode = ::write(FD, data, size);
    } while (errCode == -1 && errno == EINTR);

    if (errCode == -1) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            LOG_DEBUG(TError::FromSystem(), "Error writing");

            LastSystemError = errno;
        }
        return 0;
    } else {
        size_t bytesWritten = errCode;
        if (bytesWritten > 0) {
            LOG_DEBUG("Wrote %" PRISZT " bytes", bytesWritten);
        }

        YCHECK(bytesWritten <= size);
        return bytesWritten;
    }
}

void TAsyncWriter::Close()
{
    if (!Closed) {
        int errCode = close(FD);
        if (errCode == -1) {
            // please, read
            // http://lkml.indiana.edu/hypermail/linux/kernel/0509.1/0877.html and
            // http://rb.yandex-team.ru/arc/r/44030/
            // before editing
            if (errno != EAGAIN) {
                LOG_DEBUG(TError::FromSystem(), "Error closing");

                LastSystemError = errno;
            }
        }

        Closed = true;
        NeedToClose = false;
        FDWatcher.stop();
    }
}

TAsyncError TAsyncWriter::AsyncClose()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TGuard<TSpinLock> guard(WriteLock);

    NeedToClose = true;
    YCHECK(!ReadyPromise.HasValue());

    StartWatcher.send();

    ReadyPromise.Assign(NewPromise<TError>());
    return ReadyPromise->ToFuture();
}

TAsyncError TAsyncWriter::GetReadyEvent()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TGuard<TSpinLock> guard(WriteLock);

    if (!RegistrationError.IsSet() || !RegistrationError.Get().IsOK()) {
        return RegistrationError;
    }

    if (LastSystemError != 0) {
        return MakePromise<TError>(TError::FromSystem(LastSystemError));
    } else if (WriteBuffer.Size() < WriteBufferSize) {
        return MakePromise<TError>(TError());
    } else {
        ReadyPromise.Assign(NewPromise<TError>());
        return ReadyPromise->ToFuture();
    }
}

void TAsyncWriter::TryCleanBuffer()
{
    if (BytesWrittenTotal == WriteBuffer.Size()) {
        WriteBuffer.Clear();
        BytesWrittenTotal = 0;
    }
}

}
}
