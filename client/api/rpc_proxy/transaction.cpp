#include "transaction.h"
#include "transaction_impl.h"

namespace NYT::NApi::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

NApi::ITransactionPtr CreateTransaction(
    TConnectionPtr connection,
    TClientPtr client,
    NRpc::IChannelPtr channel,
    NTransactionClient::TTransactionId id,
    NTransactionClient::TTimestamp startTimestamp,
    NTransactionClient::ETransactionType type,
    NTransactionClient::EAtomicity atomicity,
    NTransactionClient::EDurability durability,
    TDuration timeout,
    bool pingAncestors,
    std::optional<TDuration> pingPeriod,
    bool sticky)
{
    return New<TTransaction>(
        std::move(connection),
        std::move(client),
        std::move(channel),
        id,
        startTimestamp,
        type,
        atomicity,
        durability,
        timeout,
        pingAncestors,
        pingPeriod,
        sticky);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy

