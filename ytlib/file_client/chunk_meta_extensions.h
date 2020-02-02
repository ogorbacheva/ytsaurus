#pragma once

#include <yt/ytlib/file_client/proto/file_chunk_meta.pb.h>

#include <yt/core/misc/protobuf_helpers.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

DECLARE_PROTO_EXTENSION(NFileClient::NProto::TBlocksExt, 40)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
