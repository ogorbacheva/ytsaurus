﻿#pragma once

#include <ytlib/chunk_holder/chunk.pb.h>
#include <ytlib/misc/protobuf_helpers.h>

namespace NYT {

///////////////////////////////////////////////////////////////////////////////

DECLARE_PROTO_EXTENSION(NChunkHolder::NProto::TMisc, 0)
DECLARE_PROTO_EXTENSION(NChunkHolder::NProto::TBlocks, 1)

///////////////////////////////////////////////////////////////////////////////

} // namespace NYT