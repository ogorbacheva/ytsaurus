#include "skiff.h"

#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/interface/logging/log.h>

#include <mapreduce/yt/http/retry_request.h>
#include <mapreduce/yt/http/requests.h>

#include <mapreduce/yt/interface/common.h>
#include <mapreduce/yt/interface/serialize.h>

#include <mapreduce/yt/node/node_builder.h>
#include <mapreduce/yt/node/node_io.h>

#include <mapreduce/yt/raw_client/raw_batch_request.h>
#include <mapreduce/yt/raw_client/raw_requests.h>

#include <mapreduce/yt/skiff/skiff_schema.h>

#include <library/yson/consumer.h>
#include <library/yson/writer.h>

#include <util/string/cast.h>
#include <util/stream/str.h>
#include <util/stream/file.h>
#include <util/folder/path.h>

namespace NYT {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

static NSkiff::TSkiffSchemaPtr ReadSkiffSchema(const TString& fileName)
{
    if (!TFsPath(fileName).Exists()) {
        return nullptr;
    }
    TIFStream input(fileName);
    NSkiff::TSkiffSchemaPtr schema;
    Deserialize(schema, NodeFromYsonStream(&input));
    return schema;
}

NSkiff::TSkiffSchemaPtr GetJobInputSkiffSchema()
{
    return ReadSkiffSchema("skiff_input");
}

NSkiff::EWireType ValueTypeToSkiffType(EValueType valueType)
{
    using NSkiff::EWireType;
    switch (valueType) {
        case VT_INT64:   return EWireType::Int64;
        case VT_INT32:   return EWireType::Int64;
        case VT_INT16:   return EWireType::Int64;
        case VT_INT8:    return EWireType::Int64;
        case VT_UINT64:  return EWireType::Uint64;
        case VT_UINT32:  return EWireType::Uint64;
        case VT_UINT16:  return EWireType::Uint64;
        case VT_UINT8:   return EWireType::Uint64;
        case VT_DOUBLE:  return EWireType::Double;
        case VT_BOOLEAN: return EWireType::Boolean;
        case VT_STRING:  return EWireType::String32;
        case VT_UTF8:    return EWireType::String32;
        case VT_ANY:     return EWireType::Yson32;
        default:
            ythrow yexception() << "Cannot convert EValueType '" << valueType << "' to NSkiff::EWireType";
    };
}

NSkiff::TSkiffSchemaPtr CreateSkiffSchema(
    const TTableSchema& schema,
    const TCreateSkiffSchemaOptions& options)
{
    using namespace NSkiff;

    Y_ENSURE(schema.Strict_, "Cannot create Skiff schema for non-strict table schema");
    TVector<TSkiffSchemaPtr> skiffColumns;
    for (const auto& column: schema.Columns_) {
        TSkiffSchemaPtr skiffColumn;
        if (column.Required_) {
            skiffColumn = CreateSimpleTypeSchema(ValueTypeToSkiffType(column.Type_));
        } else {
            skiffColumn = CreateVariant8Schema({
                CreateSimpleTypeSchema(EWireType::Nothing),
                CreateSimpleTypeSchema(ValueTypeToSkiffType(column.Type_))});
        }
        if (options.RenameColumns_) {
            auto maybeName = options.RenameColumns_->find(column.Name_);
            skiffColumn->SetName(maybeName == options.RenameColumns_->end() ? column.Name_ : maybeName->second);
        } else {
            skiffColumn->SetName(column.Name_);
        }
        skiffColumns.push_back(skiffColumn);
    }

    if (options.HasKeySwitch_) {
        skiffColumns.push_back(
            CreateSimpleTypeSchema(EWireType::Boolean)->SetName("$key_switch"));
    }
    if (options.HasRangeIndex_) {
        skiffColumns.push_back(
            CreateVariant8Schema({
                CreateSimpleTypeSchema(EWireType::Nothing),
                CreateSimpleTypeSchema(EWireType::Int64)})
            ->SetName("$range_index"));
    }

    skiffColumns.push_back(
        CreateVariant8Schema({
            CreateSimpleTypeSchema(EWireType::Nothing),
            CreateSimpleTypeSchema(EWireType::Int64)})
        ->SetName("$row_index"));

    return CreateTupleSchema(std::move(skiffColumns));
}

NSkiff::TSkiffSchemaPtr CreateSkiffSchema(
    const TNode& schemaNode,
    const TCreateSkiffSchemaOptions& options)
{
    TTableSchema schema;
    Deserialize(schema, schemaNode);
    return CreateSkiffSchema(schema, options);
}

void Serialize(const NSkiff::TSkiffSchemaPtr& schema, IYsonConsumer* consumer)
{
    consumer->OnBeginMap();
    if (schema->GetName().size() > 0) {
        consumer->OnKeyedItem("name");
        consumer->OnStringScalar(schema->GetName());
    }
    consumer->OnKeyedItem("wire_type");
    consumer->OnStringScalar(::ToString(schema->GetWireType()));
    if (schema->GetChildren().size() > 0) {
        consumer->OnKeyedItem("children");
        consumer->OnBeginList();
        for (const auto& child : schema->GetChildren()) {
            consumer->OnListItem();
            Serialize(child, consumer);
        }
        consumer->OnEndList();
    }
    consumer->OnEndMap();
}

void Deserialize(NSkiff::TSkiffSchemaPtr& schema, const TNode& node)
{
    using namespace NSkiff;

    static auto createSchema = [](EWireType wireType, TVector<TSkiffSchemaPtr>&& children) -> TSkiffSchemaPtr {
        switch (wireType) {
            case EWireType::Tuple:
                return CreateTupleSchema(std::move(children));
            case EWireType::Variant8:
                return CreateVariant8Schema(std::move(children));
            case EWireType::Variant16:
                return CreateVariant16Schema(std::move(children));
            case EWireType::RepeatedVariant16:
                return CreateRepeatedVariant16Schema(std::move(children));
            default:
                return CreateSimpleTypeSchema(wireType);
        }
    };

    const auto& map = node.AsMap();
    const auto* wireTypePtr = map.FindPtr("wire_type");
    Y_ENSURE(wireTypePtr, "'wire_type' is a required key");
    auto wireType = FromString<NSkiff::EWireType>(wireTypePtr->AsString());

    const auto* childrenPtr = map.FindPtr("children");
    Y_ENSURE(NSkiff::IsSimpleType(wireType) || childrenPtr,
        "'children' key is required for complex node '" << wireType << "'");
    TVector<TSkiffSchemaPtr> children;
    if (childrenPtr) {
        for (const auto& childNode : childrenPtr->AsList()) {
            TSkiffSchemaPtr childSchema;
            Deserialize(childSchema, childNode);
            children.push_back(std::move(childSchema));
        }
    }

    schema = createSchema(wireType, std::move(children));

    const auto* namePtr = map.FindPtr("name");
    if (namePtr) {
        schema->SetName(namePtr->AsString());
    }
}

TFormat CreateSkiffFormat(const NSkiff::TSkiffSchemaPtr& schema) {
    TNode node;
    TNodeBuilder nodeBuilder(&node);
    Y_ENSURE(schema->GetWireType() == NSkiff::EWireType::Variant16,
        "Bad wire type for schema; expected 'variant16', got " << schema->GetWireType());
    Serialize(schema, &nodeBuilder);
    auto config = TNode("skiff");
    config.Attributes()["table_skiff_schemas"] = node["children"];
    return TFormat(config);
}

NSkiff::TSkiffSchemaPtr CreateSkiffSchemaIfNecessary(
    const TAuth& auth,
    const TTransactionId& transactionId,
    ENodeReaderFormat nodeReaderFormat,
    const TVector<TRichYPath>& tablePaths,
    const TCreateSkiffSchemaOptions& options)
{
    if (nodeReaderFormat == ENodeReaderFormat::Yson) {
        return nullptr;
    }

    for (const auto& path : tablePaths) {
        if (path.Columns_) {
            switch (nodeReaderFormat) {
                case ENodeReaderFormat::Skiff:
                    ythrow TApiUsageError() << "Cannot use Skiff format with column selectors";
                case ENodeReaderFormat::Auto:
                    return nullptr;
                default:
                    Y_FAIL("Unexpected node reader format: %d", static_cast<int>(nodeReaderFormat));
            }
        }
    }

    TRawBatchRequest batchRequest;
    TVector<NThreading::TFuture<TNode>> tables;
    for (const auto& path : CanonizePaths(auth, tablePaths)) {
        auto getOptions = TGetOptions().AttributeFilter(TAttributeFilter().AddAttribute("schema").AddAttribute("dynamic"));
        tables.push_back(batchRequest.Get(transactionId, path.Path_, getOptions));
    }
    ExecuteBatch(auth, batchRequest, TExecuteBatchOptions());

    TVector<NSkiff::TSkiffSchemaPtr> schemas;
    for (size_t tableIndex = 0; tableIndex < tables.size(); ++tableIndex) {
        const auto& tablePath = tablePaths[tableIndex].Path_;
        const auto& attributes = tables[tableIndex].GetValue().GetAttributes();
        bool dynamic = attributes["dynamic"].AsBool();
        bool strict = attributes["schema"].GetAttributes()["strict"].AsBool();
        switch (nodeReaderFormat) {
            case ENodeReaderFormat::Skiff:
                Y_ENSURE_EX(strict,
                    TApiUsageError() << "Cannot use skiff format for table with non-strict schema '" << tablePath << "'");
                Y_ENSURE_EX(!dynamic,
                    TApiUsageError() << "Cannot use skiff format for dynamic table '" << tablePath << "'");
                break;
            case ENodeReaderFormat::Auto:
                if (dynamic || !strict) {
                    LOG_DEBUG("Cannot use skiff format for table '%s' as it is dynamic or has non-strict schema",
                        ~tablePath);
                    return nullptr;
                }
                break;
            default:
                Y_FAIL("Unexpected node reader format: %d", static_cast<int>(nodeReaderFormat));
        }
        if (tablePaths[tableIndex].RenameColumns_) {
            auto customOptions = options;
            customOptions.RenameColumns(*tablePaths[tableIndex].RenameColumns_);
            schemas.push_back(CreateSkiffSchema(attributes["schema"], customOptions));
        } else {
            schemas.push_back(CreateSkiffSchema(attributes["schema"], options));
        }
    }
    return NSkiff::CreateVariant16Schema(std::move(schemas));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NYT
