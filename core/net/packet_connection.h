#pragma once

#include "public.h"

#include <yt/core/misc/ref_counted.h>

#include <yt/core/net/address.h>

namespace NYT {
namespace NNet {

////////////////////////////////////////////////////////////////////////////////

struct IPacketConnection
    : public virtual TRefCounted
{
    virtual TFuture<std::pair<size_t, TNetworkAddress>> ReceiveFrom(const TSharedMutableRef& buffer) = 0;

    // NOTE: This method is synchronous
    virtual void SendTo(const TSharedRef& buffer, const TNetworkAddress& address) = 0;

    virtual TFuture<void> Abort() = 0;
};

DEFINE_REFCOUNTED_TYPE(IPacketConnection)

////////////////////////////////////////////////////////////////////////////////

IPacketConnectionPtr CreatePacketConnection(
    const TNetworkAddress& at,
    const NConcurrency::IPollerPtr& poller);

////////////////////////////////////////////////////////////////////////////////

} // namespace NNet
} // namespace NYT
