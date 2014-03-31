#pragma once

#include <core/misc/common.h>

namespace NYT {
namespace NNodeTrackerClient {

///////////////////////////////////////////////////////////////////////////////

typedef i32 TNodeId;
const TNodeId InvalidNodeId = 0;
const TNodeId MaxNodeId = (1 << 28) - 1; // TNodeId must fit into 28 bits (see TChunkReplica)

struct TNodeDescriptor;

class TNodeDirectory;
typedef TIntrusivePtr<TNodeDirectory> TNodeDirectoryPtr;

extern const Stroka DefaultNetworkName;

class TNodeDirectoryBuilder;

DECLARE_ENUM(EErrorCode,
    ((NoSuchNode)    (300))
    ((InvalidState)  (301))
    ((NotAuthorized) (302))
    ((InvalidNetwork)(303))
);

///////////////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerClient
} // namespace NYT
