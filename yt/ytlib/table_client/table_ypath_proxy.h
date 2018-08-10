#pragma once

#include "public.h"

#include <yt/ytlib/chunk_client/chunk_owner_ypath_proxy.h>

#include <yt/ytlib/table_client/table_ypath.pb.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

struct TTableYPathProxy
    : public NChunkClient::TChunkOwnerYPathProxy
{
    DEFINE_YPATH_PROXY(Table);

    DEFINE_YPATH_PROXY_METHOD(NProto, GetMountInfo);
    DEFINE_MUTATING_YPATH_PROXY_METHOD(NProto, Alter);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
