#include "types_translation.h"

#include <yt/ytlib/table_client/schema.h>

#include <yt/client/table_client/row_base.h>

#include <yt/core/misc/error.h>

#include <util/generic/hash.h>

namespace NYT {
namespace NClickHouse {

using namespace NYT::NTableClient;

////////////////////////////////////////////////////////////////////////////////

// YQL types

static const THashMap<TString, NInterop::EColumnType> YqlToInteropTypes = {
    /// Signed integer value.
    { "Int8",     NInterop::EColumnType::Int8 },
    { "Int16",    NInterop::EColumnType::Int16 },
    { "Int32",    NInterop::EColumnType::Int32 },
    { "Int64",    NInterop::EColumnType::Int64 },

    /// Unsigned integer value.
    { "Uint8",    NInterop::EColumnType::UInt8 },
    { "Uint16",   NInterop::EColumnType::UInt16 },
    { "Uint32",   NInterop::EColumnType::UInt32 },
    { "Uint64",   NInterop::EColumnType::UInt64 },

    /// Floating point value.
    { "Float",    NInterop::EColumnType::Float },
    { "Double",   NInterop::EColumnType::Double },

    /// Boolean value.
    { "Boolean",  NInterop::EColumnType::Boolean },

    /// DateTime value.
    { "Date",     NInterop::EColumnType::Date },
    { "DateTime", NInterop::EColumnType::DateTime },

    /// String value.
    { "String",   NInterop::EColumnType::String },
};

bool IsYqlTypeSupported(TStringBuf typeName)
{
    return YqlToInteropTypes.find(typeName) != YqlToInteropTypes.end();
}

NInterop::EColumnType RepresentYqlType(TStringBuf typeName)
{
    auto found = YqlToInteropTypes.find(typeName);
    if (found == YqlToInteropTypes.end()) {
        THROW_ERROR_EXCEPTION("YQL type %Qv not supported", typeName);
    }
    return found->second;
}


static const THashMap<TString, EValueType> YqlToUnderlyingYtTypes = {
    /// Signed integer value.
    { "Int8",     EValueType::Int64 },
    { "Int16",    EValueType::Int64 },
    { "Int32",    EValueType::Int64 },
    { "Int64",    EValueType::Int64 },

    /// Unsigned integer value.
    { "Uint8",    EValueType::Uint64 },
    { "Uint16",   EValueType::Uint64 },
    { "Uint32",   EValueType::Uint64 },
    { "Uint64",   EValueType::Uint64 },

    /// Floating point value.
    { "Float",    EValueType::Double },
    { "Double",   EValueType::Double },

    /// Boolean value.
    { "Boolean",  EValueType::Boolean },

    /// DateTime value.
    { "Date",     EValueType::Uint64 },
    { "DateTime", EValueType::Uint64 },

    /// String value.
    { "String",   EValueType::String },
};

EValueType GetYqlUnderlyingYtType(TStringBuf typeName)
{
    auto found = YqlToUnderlyingYtTypes.find(typeName);
    if (found == YqlToUnderlyingYtTypes.end()) {
        THROW_ERROR_EXCEPTION("YQL type %Qlv not supported", typeName);
    }
    return found->second;
}

////////////////////////////////////////////////////////////////////////////////

// YT native types

bool IsYtTypeSupported(EValueType valueType)
{
    switch (valueType) {
        case EValueType::Int64:
        case EValueType::Uint64:
        case EValueType::Double:
        case EValueType::Boolean:
        case EValueType::String:
            return true;

        case EValueType::Null:
        case EValueType::Any:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            return false;
    };

    THROW_ERROR_EXCEPTION("Unexpected YT value type: %Qlv", valueType);
}

NInterop::EColumnType RepresentYtType(EValueType valueType)
{
    switch (valueType) {
        /// Signed integer value.
        case EValueType::Int64:
            return NInterop::EColumnType::Int64;

        /// Unsigned integer value.
        case EValueType::Uint64:
            return NInterop::EColumnType::UInt64;

        /// Floating point value.
        case EValueType::Double:
            return NInterop::EColumnType::Double;

        /// Boolean value.
        case EValueType::Boolean:
            return NInterop::EColumnType::Boolean;

        /// String value.
        case EValueType::String:
            return NInterop::EColumnType::String;

        case EValueType::Null:
        case EValueType::Any:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            break;
    }

    THROW_ERROR_EXCEPTION("YT value type %Qlv not supported", valueType);
}

} // namespace NClickHouse
} // namespace NYT
