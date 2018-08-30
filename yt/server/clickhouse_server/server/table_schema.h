#pragma once

#include "public.h"

#include <yt/server/clickhouse_server/interop/api.h>

#include <yt/ytlib/table_client/public.h>

#include <yt/core/yson/public.h>

#include <util/generic/string.h>

namespace NYT {
namespace NClickHouse {

////////////////////////////////////////////////////////////////////////////////

NInterop::TTablePtr CreateTableSchema(
    const TString& name,
    const NTableClient::TTableSchema& schema);

}   // namespace NClickHouse
}   // namespace NYT
