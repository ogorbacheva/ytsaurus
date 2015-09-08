#pragma once

#include <core/misc/public.h>

#include <ytlib/chunk_client/chunk_owner_ypath.pb.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>

#include <core/ytree/ypath_proxy.h>

namespace NYT {
namespace NChunkClient {

///////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EUpdateMode,
    (None)
    (Append)
    (Overwrite)
);

struct TChunkOwnerYPathProxy
    : public NCypressClient::TCypressYPathProxy
{
    static Stroka GetServiceName()
    {
        return "ChunkOwner";
    }

    DEFINE_YPATH_PROXY_METHOD(NProto, Fetch);
    DEFINE_MUTATING_YPATH_PROXY_METHOD(NProto, BeginUpload);
    DEFINE_YPATH_PROXY_METHOD(NProto, GetUploadParams);
    DEFINE_MUTATING_YPATH_PROXY_METHOD(NProto, EndUpload);
};

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
