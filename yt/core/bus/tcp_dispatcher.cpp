#include "tcp_dispatcher.h"
#include "tcp_dispatcher_impl.h"

#include <yt/core/misc/common.h>
#include <yt/core/misc/singleton.h>

#include <yt/core/profiling/profile_manager.h>

namespace NYT {
namespace NBus {

////////////////////////////////////////////////////////////////////////////////

TTcpDispatcherStatistics operator + (
    const TTcpDispatcherStatistics& lhs,
    const TTcpDispatcherStatistics& rhs)
{
    auto result = lhs;
    result += rhs;
    return result;
}

TTcpDispatcherStatistics& operator += (
    TTcpDispatcherStatistics& lhs,
    const TTcpDispatcherStatistics& rhs)
{
    lhs.InBytes += rhs.InBytes;
    lhs.InPackets += rhs.InPackets;
    lhs.OutBytes += rhs.OutBytes;
    lhs.OutPackets += rhs.OutPackets;
    lhs.PendingOutBytes += rhs.PendingOutBytes;
    lhs.PendingOutPackets += rhs.PendingOutPackets;
    lhs.ClientConnections += rhs.ClientConnections;
    lhs.ServerConnections += rhs.ServerConnections;
    return lhs;
}

////////////////////////////////////////////////////////////////////////////////

TTcpDispatcher::TTcpDispatcher()
    : Impl_(new TImpl())
{ }

TTcpDispatcher::~TTcpDispatcher()
{ }

TTcpDispatcher* TTcpDispatcher::Get()
{
    return TSingletonWithFlag<TTcpDispatcher>::Get();
}

void TTcpDispatcher::StaticShutdown()
{
    if (TSingletonWithFlag<TTcpDispatcher>::WasCreated()) {
        TSingletonWithFlag<TTcpDispatcher>::Get()->Shutdown();
    }
}

void TTcpDispatcher::Shutdown()
{
    Impl_->Shutdown();
}

TTcpDispatcherStatistics TTcpDispatcher::GetStatistics(ETcpInterfaceType interfaceType)
{
    return Impl_->GetStatistics(interfaceType);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NBus
} // namespace NYT
