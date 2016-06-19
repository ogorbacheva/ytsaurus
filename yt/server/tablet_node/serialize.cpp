#include "serialize.h"

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

int GetCurrentSnapshotVersion()
{
    return 17;
}

bool ValidateSnapshotVersion(int version)
{
    return version == 16 ||
           version == 17;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
