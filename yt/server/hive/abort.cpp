#include <yt/core/misc/ref.h>
#include "abort.h"

namespace NYT {
namespace NHiveServer {

////////////////////////////////////////////////////////////////////////////////

TAbort::TAbort(
    const TTransactionId& transactionId,
    const NRpc::TMutationId& mutationId)
    : TransactionId_(transactionId)
    , MutationId_(mutationId)
{ }

TFuture<TSharedRefArray> TAbort::GetAsyncResponseMessage()
{
    return ResponseMessagePromise_;
}

void TAbort::SetResponseMessage(TSharedRefArray message)
{
    ResponseMessagePromise_.Set(std::move(message));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NHiveServer
} // namespace NYT
