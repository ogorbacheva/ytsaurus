#include "file_writer.h"

#include <yt/client/api/file_writer.h>

#include <yt/core/rpc/stream.h>

namespace NYT::NApi::NRpcProxy {

using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

class TRpcFileWriter
    : public IFileWriter
{
public:
    TRpcFileWriter(
        TApiServiceProxy::TReqWriteFilePtr request)
        : Request_(std::move(request))
    {
        YCHECK(Request_);
    }

    virtual TFuture<void> Open() override
    {
        ValidateNotClosed();

        auto guard = Guard(SpinLock_);
        if (!OpenResult_) {
            OpenResult_ = NRpc::CreateOutputStreamAdapter(Request_)
                .Apply(BIND([=, this_ = MakeStrong(this)] (const IAsyncZeroCopyOutputStreamPtr& outputStream) {
                    Underlying_ = outputStream;
                })).As<void>();
        }

        return OpenResult_;
    }

    virtual TFuture<void> Write(const TSharedRef& data) override
    {
        ValidateOpened();
        ValidateNotClosed();

        if (!data) {
            return VoidFuture;
        }

        // Data can be rewritten after returned future is set, which can happen prematurely.
        struct TTag { };
        auto dataCopy = TSharedMutableRef::MakeCopy<TTag>(data);
        return Underlying_->Write(dataCopy);
    }

    virtual TFuture<void> Close() override
    {
        ValidateOpened();
        ValidateNotClosed();

        Closed_ = true;
        return Underlying_->Close();
    }

private:
    const TApiServiceProxy::TReqWriteFilePtr Request_;

    IAsyncZeroCopyOutputStreamPtr Underlying_;
    TFuture<void> OpenResult_;
    std::atomic<bool> Closed_ = {false};

    TSpinLock SpinLock_;

    void ValidateOpened()
    {
        auto guard = Guard(SpinLock_);
        if (!OpenResult_ || !OpenResult_.IsSet()) {
            THROW_ERROR_EXCEPTION("Can't write into an unopened file writer");
        }
        OpenResult_.Get().ThrowOnError();
    }

    void ValidateNotClosed()
    {
        if (Closed_) {
            THROW_ERROR_EXCEPTION("File writer is closed");
        }
    }
};

IFileWriterPtr CreateRpcFileWriter(
    TApiServiceProxy::TReqWriteFilePtr request)
{
    return New<TRpcFileWriter>(std::move(request));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy

