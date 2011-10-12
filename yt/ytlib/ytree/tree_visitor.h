#pragma once

#include "common.h"
#include "ytree.h"
#include "yson_events.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TTreeVisitor
    : private TNonCopyable
{
public:
    TTreeVisitor(IYsonConsumer* events)
        : Events(events)
    { }

    void Visit(INode::TPtr root)
    {
        VisitAny(root);
    }

private:
    IYsonConsumer* Events;

    void VisitAny(INode::TPtr node)
    {
        switch (node->GetType()) {
            case ENodeType::String:
            case ENodeType::Int64:
            case ENodeType::Double:
            case ENodeType::Entity:
                VisitScalar(node);
                break;

            case ENodeType::List:
                VisitList(node->AsList());
                break;

            case ENodeType::Map:
                VisitMap(node->AsMap());
                break;

            default:
                YASSERT(false);
                break;
        }

        auto attributes = node->GetAttributes();
        if (~attributes != NULL) {
            VisitAttributes(attributes);
        }
    }

    void VisitScalar(INode::TPtr node)
    {
        switch (node->GetType()) {
            case ENodeType::String:
                Events->StringScalar(node->GetValue<Stroka>());
                break;

            case ENodeType::Int64:
                Events->Int64Scalar(node->GetValue<i64>());
                break;

            case ENodeType::Double:
                Events->DoubleScalar(node->GetValue<double>());
                break;

            case ENodeType::Entity:
                Events->EntityScalar();
                break;

            default:
                YASSERT(false);
                break;
        }
    }

    void VisitList(IListNode::TPtr node)
    {
        Events->BeginList();
        for (int i = 0; i < node->GetChildCount(); ++i) {
            auto child = node->GetChild(i);
            Events->ListItem(i);
            VisitAny(child);
        }
        Events->EndList();
    }

    void VisitMap(IMapNode::TPtr node)
    {
        Events->BeginMap();
        FOREACH(const auto& pair, node->GetChildren()) {
            Events->MapItem(pair.First());
            VisitAny(pair.Second());
        }
        Events->EndMap();
    }

    void VisitAttributes(IMapNode::TPtr node)
    {
        Events->BeginAttributes();
        FOREACH(const auto& pair, node->GetChildren()) {
            Events->AttributesItem(pair.First());
            VisitAny(pair.Second());
        }
        Events->EndAttributes();
    }

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
