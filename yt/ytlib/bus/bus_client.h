#pragma once

#include "common.h"
#include "bus.h"
#include "message.h"

#include <quality/NetLiba/UdpHttp.h>
#include <quality/NetLiba/UdpAddress.h>

namespace NYT {
namespace NBus {

////////////////////////////////////////////////////////////////////////////////

class TClientDispatcher;

class TBusClient
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TBusClient> TPtr;

    TBusClient(Stroka address);

    IBus::TPtr CreateBus(IMessageHandler::TPtr handler);

private:
    class TBus;
    friend class TClientDispatcher;

    TUdpAddress ServerAddress;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NBus
} // namespace NYT
