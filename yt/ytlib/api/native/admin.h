#pragma once

#include "public.h"

#include <yt/ytlib/api/connection.h>

namespace NYT {
namespace NApi {
namespace NNative {

////////////////////////////////////////////////////////////////////////////////

IAdminPtr CreateAdmin(
    IConnectionPtr connection,
    const TAdminOptions& options);

////////////////////////////////////////////////////////////////////////////////

} // namespace NNative
} // namespace NApi
} // namespace NYT

