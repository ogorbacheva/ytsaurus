#include "protobuf.h"

#include <mapreduce/yt/interface/protos/extension.pb.h>

namespace NYT {
    TString GetColumnName(const ::google::protobuf::FieldDescriptor& field) {
        const auto& options = field.options();
        const auto columnName = options.GetExtension(column_name);
        if (!columnName.empty()) {
            return columnName;
        }
        const auto keyColumnName = options.GetExtension(key_column_name);
        if (!keyColumnName.empty()) {
            return keyColumnName;
        }
        return field.name();
    }

    EValueType GetColumnType(const ::google::protobuf::FieldDescriptor& field) {
        using namespace ::google::protobuf;
        Y_ENSURE(!field.is_repeated());
        switch (field.cpp_type()) {
        case FieldDescriptor::CPPTYPE_INT32:
        case FieldDescriptor::CPPTYPE_INT64:
            return EValueType::VT_INT64;
        case FieldDescriptor::CPPTYPE_UINT32:
        case FieldDescriptor::CPPTYPE_UINT64:
            return EValueType::VT_UINT64;
        case FieldDescriptor::CPPTYPE_FLOAT:
        case FieldDescriptor::CPPTYPE_DOUBLE:
            return EValueType::VT_DOUBLE;
        case FieldDescriptor::CPPTYPE_BOOL:
            return EValueType::VT_BOOLEAN;
        case FieldDescriptor::CPPTYPE_STRING:
        case FieldDescriptor::CPPTYPE_MESSAGE:
        case FieldDescriptor::CPPTYPE_ENUM:
            return EValueType::VT_STRING;
        default:
            ythrow yexception() << "Unexpected field type '" << field.cpp_type_name() << "' for field " << field.name();
        }
    }

    TTableSchema CreateTableSchema(const ::google::protobuf::Descriptor& tableProto, const TKeyColumns& keyColumns) {
        TTableSchema result;
        auto keyIt = keyColumns.Parts_.begin();
        const auto keyLim = keyColumns.Parts_.end();
        for (int idx = 0, lim = tableProto.field_count(); idx < lim; ++idx) {
            const auto field = tableProto.field(idx);
            const auto name = GetColumnName(*field);
            TColumnSchema column;
            column.Name(name);
            column.Type(GetColumnType(*field));
            if (keyIt != keyLim) {
                Y_ENSURE(*keyIt == name, "Key columns list should be prefix of schema");
                column.SortOrder(SO_ASCENDING);
                ++keyIt;
            }
            result.AddColumn(column);
        }
        return result;
    }
}
