#include "helpers.h"

#include <yt/core/misc/proc.h>

#include <yt/core/net/socket.h>

namespace NYT {
namespace NNet {

////////////////////////////////////////////////////////////////////////////////

std::vector<int> AllocateFreePorts(
    int portCount,
    const THashSet<int>& availablePorts,
    const NLogging::TLogger& logger)
{
    if (portCount == 0) {
        return {};
    }

    const auto& Logger = logger;

    // Here goes our best effort to make sure we provide free ports to user job.
    // No doubt there may still be race conditions in which user job will still not be
    // able to bind to the port, but it should happen pretty rarely.
    std::vector<int> allocatedPorts;

    for (int port : availablePorts) {
        SOCKET socket = INVALID_SOCKET;

        try {
            socket = CreateTcpServerSocket();
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error while creating a socket for preliminary port bind")
                << ex;
        }

        YCHECK(socket != INVALID_SOCKET);

        try {
            LOG_DEBUG("Making a preliminary port bind (Port: %v, Socket: %v)", port, socket);
            BindSocket(socket, TNetworkAddress::CreateIPv6Any(port));
        } catch (const std::exception& ex) {
            SafeClose(socket, false /* ignoreBadFD */);
            LOG_DEBUG(ex, "Error while trying making a preliminary port bind, skipping it (Port: %v, Socket: %v)", port, socket);
            continue;
        }

        SafeClose(socket, false /* ignoreBadFD */);
        LOG_DEBUG("Socket used in preliminary bind is closed (Port: %v, Socket: %v)", port, socket);

        allocatedPorts.push_back(port);

        if (allocatedPorts.size() >= portCount) {
            break;
        }
    }

    YCHECK(allocatedPorts.size() == portCount);

    return allocatedPorts;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NNet
} // namespace NYT
