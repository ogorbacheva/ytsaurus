#pragma once

#include <yt/server/clickhouse_server/interop/table_schema.h>

#include <yt/ytlib/table_client/public.h>

#include <util/generic/strbuf.h>

namespace NYT {
namespace NClickHouse {

////////////////////////////////////////////////////////////////////////////////

// YT native types

bool IsYtTypeSupported(NTableClient::EValueType valueType);

NInterop::EColumnType RepresentYtType(NTableClient::EValueType valueType);

} // namespace NClickHouse
} // namespace NYT
