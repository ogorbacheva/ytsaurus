#include "json_callbacks.h"

#include <yt/core/ytree/ephemeral_node_factory.h>
#include <yt/core/ytree/node.h>
#include <yt/core/ytree/tree_builder.h>

#include <yt/core/ytree/convert.h>

namespace NYT {
namespace NFormats {

using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

TJsonCallbacksBuildingNodesImpl::TJsonCallbacksBuildingNodesImpl(
    IYsonConsumer* consumer,
    NYson::EYsonType ysonType,
const TUtf8Transcoder& utf8Transcoder,
    i64 memoryLimit)
    : Consumer_(consumer)
    , YsonType_(ysonType)
    , Utf8Transcoder_(utf8Transcoder)
    , MemoryLimit_(memoryLimit)
    , TreeBuilder_(CreateBuilderFromFactory(GetEphemeralNodeFactory()))
{
    TreeBuilder_->BeginTree();
}

void TJsonCallbacksBuildingNodesImpl::OnStringScalar(const TStringBuf& value)
{
    AccountMemory(value.Size());
    OnItemStarted();
    TreeBuilder_->OnStringScalar(Utf8Transcoder_.Decode(value));
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::OnInt64Scalar(i64 value)
{
    AccountMemory(sizeof(value));
    OnItemStarted();
    TreeBuilder_->OnInt64Scalar(value);
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::OnUint64Scalar(ui64 value)
{
    AccountMemory(sizeof(value));
    OnItemStarted();
    TreeBuilder_->OnUint64Scalar(value);
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::OnDoubleScalar(double value)
{
    AccountMemory(sizeof(value));
    OnItemStarted();
    TreeBuilder_->OnDoubleScalar(value);
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::OnBooleanScalar(bool value)
{
    AccountMemory(sizeof(value));
    OnItemStarted();
    TreeBuilder_->OnBooleanScalar(value);
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::OnEntity()
{
    AccountMemory(0);
    OnItemStarted();
    TreeBuilder_->OnEntity();
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::OnBeginList()
{
    AccountMemory(0);
    OnItemStarted();
    TreeBuilder_->OnBeginList();
    Stack_.push_back(EJsonCallbacksNodeType::List);
}

void TJsonCallbacksBuildingNodesImpl::OnEndList()
{
    TreeBuilder_->OnEndList();
    Stack_.pop_back();
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::OnBeginMap()
{
    AccountMemory(0);
    OnItemStarted();
    TreeBuilder_->OnBeginMap();
    Stack_.push_back(EJsonCallbacksNodeType::Map);
}

void TJsonCallbacksBuildingNodesImpl::OnKeyedItem(const TStringBuf& key)
{
    AccountMemory(sizeof(key.size()));
    TreeBuilder_->OnKeyedItem(Utf8Transcoder_.Decode(key));
}

void TJsonCallbacksBuildingNodesImpl::OnEndMap()
{
    TreeBuilder_->OnEndMap();
    Stack_.pop_back();
    OnItemFinished();
}

void TJsonCallbacksBuildingNodesImpl::AccountMemory(i64 memory)
{
    memory += sizeof(NYTree::INodePtr);
    if (ConsumedMemory_ + memory > MemoryLimit_) {
        THROW_ERROR_EXCEPTION(
            "Memory limit exceeded while parsing JSON: allocated %v, limit %v",
            ConsumedMemory_ + memory,
            MemoryLimit_);
    }
    ConsumedMemory_ += memory;
}

void TJsonCallbacksBuildingNodesImpl::OnItemStarted()
{
    if (!Stack_.empty() && Stack_.back() == EJsonCallbacksNodeType::List)
    {
        TreeBuilder_->OnListItem();
    }
}

void TJsonCallbacksBuildingNodesImpl::OnItemFinished()
{
    if (Stack_.empty()) {
        if (YsonType_ == EYsonType::ListFragment) {
            Consumer_->OnListItem();
        }
        ConsumeNode(TreeBuilder_->EndTree());
        TreeBuilder_->BeginTree();
        ConsumedMemory_ = 0;
    }
}

void TJsonCallbacksBuildingNodesImpl::ConsumeNode(INodePtr node)
{
    switch (node->GetType()) {
        case ENodeType::Int64:
            Consumer_->OnInt64Scalar(node->AsInt64()->GetValue());
            break;
        case ENodeType::Uint64:
            Consumer_->OnUint64Scalar(node->AsUint64()->GetValue());
            break;
        case ENodeType::Double:
            Consumer_->OnDoubleScalar(node->AsDouble()->GetValue());
            break;
        case ENodeType::Boolean:
            Consumer_->OnBooleanScalar(node->AsBoolean()->GetValue());
            break;
        case ENodeType::Entity:
            Consumer_->OnEntity();
            break;
        case ENodeType::String:
            Consumer_->OnStringScalar(node->AsString()->GetValue());
            break;
        case ENodeType::Map:
            ConsumeNode(node->AsMap());
            break;
        case ENodeType::List:
            ConsumeNode(node->AsList());
            break;
        default:
            YUNREACHABLE();
            break;
    };
}

void TJsonCallbacksBuildingNodesImpl::ConsumeMapFragment(IMapNodePtr map)
{
    for (const auto& pair : map->GetChildren()) {
        auto key = TStringBuf(pair.first);
        const auto& value = pair.second;
        if (IsSpecialJsonKey(key)) {
            if (key.size() < 2 || key[1] != '$') {
                THROW_ERROR_EXCEPTION(
                    "Key \"%v\" starts with single \"$\"; use \"$%v\" "
                    "to encode this key in JSON format",
                    key,
                    key);
            }
            key = key.substr(1);
        }
        Consumer_->OnKeyedItem(key);
        ConsumeNode(value);
    }
}

void TJsonCallbacksBuildingNodesImpl::ConsumeNode(IMapNodePtr map)
{
    auto node = map->FindChild("$value");
    if (node) {
        auto attributes = map->FindChild("$attributes");
        if (attributes) {
            if (attributes->GetType() != ENodeType::Map) {
                THROW_ERROR_EXCEPTION("Value of \"$attributes\" must be a map");
            }
            Consumer_->OnBeginAttributes();
            ConsumeMapFragment(attributes->AsMap());
            Consumer_->OnEndAttributes();
        }

        auto type = map->FindChild("$type");

        if (type) {
            if (type->GetType() != ENodeType::String) {
                THROW_ERROR_EXCEPTION("Value of \"$type\" must be a string");
            }
            auto typeString = type->AsString()->GetValue();
            ENodeType expectedType;
            if (typeString == "string") {
                expectedType = ENodeType::String;
            } else if (typeString == "int64") {
                expectedType = ENodeType::Int64;
            } else if (typeString == "uint64") {
                expectedType = ENodeType::Uint64;
            } else if (typeString == "double") {
                expectedType = ENodeType::Double;
            } else if (typeString == "boolean") {
                expectedType = ENodeType::Boolean;
            } else {
                THROW_ERROR_EXCEPTION("Unexpected \"$type\" value %Qv", typeString);
            }

            if (node->GetType() == expectedType) {
                ConsumeNode(node);
            } else if (node->GetType() == ENodeType::String) {
                auto nodeAsString = node->AsString()->GetValue();
                switch (expectedType) {
                    case ENodeType::Int64:
                        Consumer_->OnInt64Scalar(FromString<i64>(nodeAsString));
                        break;
                    case ENodeType::Uint64:
                        Consumer_->OnUint64Scalar(FromString<ui64>(nodeAsString));
                        break;
                    case ENodeType::Double:
                        Consumer_->OnDoubleScalar(FromString<double>(nodeAsString));
                        break;
                    case ENodeType::Boolean: {
                        if (nodeAsString == "true") {
                            Consumer_->OnBooleanScalar(true);
                        } else if (nodeAsString == "false") {
                            Consumer_->OnBooleanScalar(false);
                        } else {
                            THROW_ERROR_EXCEPTION("Invalid boolean string %Qv", nodeAsString);
                        }
                        break;
                    }
                    default:
                        YUNREACHABLE();
                        break;
                }
            } else if (node->GetType() == ENodeType::Int64) {
                auto nodeAsInt = node->AsInt64()->GetValue();
                switch (expectedType) {
                    case ENodeType::Int64:
                        Consumer_->OnInt64Scalar(nodeAsInt);
                        break;
                    case ENodeType::Uint64:
                        Consumer_->OnUint64Scalar(nodeAsInt);
                        break;
                    case ENodeType::Double:
                        Consumer_->OnDoubleScalar(nodeAsInt);
                        break;
                    case ENodeType::Boolean:
                    case ENodeType::String:
                        THROW_ERROR_EXCEPTION("Type mismatch in JSON")
                            << TErrorAttribute("expected_type", expectedType)
                            << TErrorAttribute("actual_type", node->GetType());
                        break;
                    default:
                        YUNREACHABLE();
                        break;
                }
            } else {
                THROW_ERROR_EXCEPTION("Type mismatch in JSON")
                    << TErrorAttribute("expected_type", expectedType)
                    << TErrorAttribute("actual_type", node->GetType());
            }
        } else {
            ConsumeNode(node);
        }
    } else {
        if (map->FindChild("$attributes")) {
            THROW_ERROR_EXCEPTION("Found key \"$attributes\" without key \"$value\"");
        }
        Consumer_->OnBeginMap();
        ConsumeMapFragment(map);
        Consumer_->OnEndMap();
    }
}

void TJsonCallbacksBuildingNodesImpl::ConsumeNode(IListNodePtr list)
{
    Consumer_->OnBeginList();
    for (int i = 0; i < list->GetChildCount(); ++i) {
        Consumer_->OnListItem();
        ConsumeNode(list->GetChild(i));
    }
    Consumer_->OnEndList();
}

////////////////////////////////////////////////////////////////////////////////

TJsonCallbacksForwardingImpl::TJsonCallbacksForwardingImpl(
    IYsonConsumer* consumer,
    NYson::EYsonType ysonType,
    const TUtf8Transcoder& utf8Transcoder)
    : Consumer_(consumer)
    , YsonType_(ysonType)
    , Utf8Transcoder_(utf8Transcoder)
{ }

void TJsonCallbacksForwardingImpl::OnStringScalar(const TStringBuf& value)
{
    OnItemStarted();
    Consumer_->OnStringScalar(Utf8Transcoder_.Decode(value));
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnInt64Scalar(i64 value)
{
    OnItemStarted();
    Consumer_->OnInt64Scalar(value);
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnUint64Scalar(ui64 value)
{
    OnItemStarted();
    Consumer_->OnUint64Scalar(value);
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnDoubleScalar(double value)
{
    OnItemStarted();
    Consumer_->OnDoubleScalar(value);
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnBooleanScalar(bool value)
{
    OnItemStarted();
    Consumer_->OnBooleanScalar(value);
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnEntity()
{
    OnItemStarted();
    Consumer_->OnEntity();
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnBeginList()
{
    OnItemStarted();
    Stack_.push_back(EJsonCallbacksNodeType::List);
    Consumer_->OnBeginList();
}

void TJsonCallbacksForwardingImpl::OnEndList()
{
    Consumer_->OnEndList();
    Stack_.pop_back();
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnBeginMap()
{
    OnItemStarted();
    Stack_.push_back(EJsonCallbacksNodeType::Map);
    Consumer_->OnBeginMap();
}

void TJsonCallbacksForwardingImpl::OnKeyedItem(const TStringBuf& key)
{
    Consumer_->OnKeyedItem(Utf8Transcoder_.Decode(key));
}

void TJsonCallbacksForwardingImpl::OnEndMap()
{
    Consumer_->OnEndMap();
    Stack_.pop_back();
    OnItemFinished();
}

void TJsonCallbacksForwardingImpl::OnItemStarted()
{
    if ((Stack_.empty() && YsonType_ == EYsonType::ListFragment) || (!Stack_.empty() && Stack_.back() == EJsonCallbacksNodeType::List))
    {
        Consumer_->OnListItem();
    }
}

void TJsonCallbacksForwardingImpl::OnItemFinished()
{ }

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
