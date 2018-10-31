#pragma once

#include <yt/server/clickhouse_server/native/public.h>

#include <Core/Field.h>
#include <Core/NamesAndTypes.h>

#include <string>
#include <vector>

namespace NYT {
namespace NClickHouseServer {
namespace NEngine {

////////////////////////////////////////////////////////////////////////////////

const char* GetTypeName(const NNative::TColumn& column);

DB::DataTypePtr GetDataType(const std::string& name);

DB::NamesAndTypesList GetTableColumns(const NNative::TTable& table);

std::vector<DB::Field> GetFields(const NNative::TValue* values, size_t count);

} // namespace NEngine
} // namespace NClickHouseServer
} // namespace NYT
