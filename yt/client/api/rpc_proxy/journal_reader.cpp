#include "journal_reader.h"

#include <yt/client/api/journal_reader.h>

#include <yt/core/rpc/stream.h>

namespace NYT::NApi::NRpcProxy {

using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

class TRpcJournalReader
    : public IJournalReader
{
public:
    TRpcJournalReader(
        TApiServiceProxy::TReqReadJournalPtr request)
        : Request_(std::move(request))
    {
        YCHECK(Request_);
    }

    virtual TFuture<void> Open() override
    {
        auto guard = Guard(SpinLock_);

        if (!OpenResult_) {
            OpenResult_ = NRpc::CreateInputStreamAdapter(Request_)
                .Apply(BIND([=, this_ = MakeStrong(this)] (const IAsyncZeroCopyInputStreamPtr& inputStream) {
                    Underlying_ = inputStream;
                }));
        }

        return OpenResult_;
    }

    virtual TFuture<std::vector<TSharedRef>> Read() override
    {
        ValidateOpened();

        return Underlying_->Read().Apply(BIND ([] (const TSharedRef& packedRows) {
            std::vector<TSharedRef> rows;
            if (packedRows) {
                UnpackRefs(packedRows, &rows, true);
            }
            return rows;
        }));
    }

private:
    const TApiServiceProxy::TReqReadJournalPtr Request_;

    IAsyncZeroCopyInputStreamPtr Underlying_;
    TSpinLock SpinLock_;
    TFuture<void> OpenResult_;

    void ValidateOpened()
    {
        auto guard = Guard(SpinLock_);
        if (!OpenResult_ || !OpenResult_.IsSet()) {
            THROW_ERROR_EXCEPTION("Can't read from an unopened journal reader");
        }
        OpenResult_.Get().ThrowOnError();
    }
};

IJournalReaderPtr CreateRpcJournalReader(
    TApiServiceProxy::TReqReadJournalPtr request)
{
    return New<TRpcJournalReader>(std::move(request));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy

