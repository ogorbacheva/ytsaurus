#pragma once

#include <yt/server/clickhouse_server/interop/api.h>

#include <Core/Field.h>
#include <Core/NamesAndTypes.h>

#include <string>
#include <vector>

namespace NYT {
namespace NClickHouse {

////////////////////////////////////////////////////////////////////////////////

const char* GetTypeName(const NInterop::TColumn& column);

DB::DataTypePtr GetDataType(const std::string& name);

DB::NamesAndTypesList GetTableColumns(const NInterop::TTable& table);

std::vector<DB::Field> GetFields(const NInterop::TValue* values, size_t count);

}   // namespace NClickHouse
}   // namespace NYT
