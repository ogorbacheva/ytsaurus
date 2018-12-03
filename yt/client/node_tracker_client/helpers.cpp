#include "helpers.h"

namespace NYT {
namespace NNodeTrackerClient {

////////////////////////////////////////////////////////////////////////////////

NYPath::TYPath GetClusterNodesPath()
{
    return "//sys/cluster_nodes";
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerClient
} // namespace NYT

