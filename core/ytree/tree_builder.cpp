#include "tree_builder.h"
#include "attribute_consumer.h"
#include "attribute_helpers.h"
#include "attributes.h"
#include "forwarding_yson_consumer.h"
#include "node.h"

#include <yt/core/actions/bind.h>

#include <yt/core/misc/assert.h>
#include <yt/core/misc/common.h>

#include <stack>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TTreeBuilder
    : public TForwardingYsonConsumer
    , public ITreeBuilder
{
public:
    explicit TTreeBuilder(INodeFactoryPtr factory)
        : Factory(factory)
    { }

    virtual void BeginTree() override
    {
        YCHECK(NodeStack.size() == 0);
    }

    virtual INodePtr EndTree() override
    {
        // Failure here means that the tree is not fully constructed yet.
        YCHECK(NodeStack.size() == 0);
        YCHECK(ResultNode);

        return ResultNode;
    }

    virtual void OnNode(INodePtr node) override
    {
        AddNode(node, false);
    }

    virtual void OnMyStringScalar(const TStringBuf& value) override
    {
        auto node = Factory->CreateString();
        node->SetValue(Stroka(value));
        AddNode(node, false);
    }

    virtual void OnMyInt64Scalar(i64 value) override
    {
        auto node = Factory->CreateInt64();
        node->SetValue(value);
        AddNode(node, false);
    }

    virtual void OnMyUint64Scalar(ui64 value) override
    {
        auto node = Factory->CreateUint64();
        node->SetValue(value);
        AddNode(node, false);
    }

    virtual void OnMyDoubleScalar(double value) override
    {
        auto node = Factory->CreateDouble();
        node->SetValue(value);
        AddNode(node, false);
    }

    virtual void OnMyBooleanScalar(bool value) override
    {
        auto node = Factory->CreateBoolean();
        node->SetValue(value);
        AddNode(node, false);
    }

    virtual void OnMyEntity() override
    {
        AddNode(Factory->CreateEntity(), false);
    }


    virtual void OnMyBeginList() override
    {
        AddNode(Factory->CreateList(), true);
    }

    virtual void OnMyListItem() override
    {
        Y_ASSERT(!Key);
    }

    virtual void OnMyEndList() override
    {
        NodeStack.pop();
    }


    virtual void OnMyBeginMap() override
    {
        AddNode(Factory->CreateMap(), true);
    }

    virtual void OnMyKeyedItem(const TStringBuf& key) override
    {
        Key = Stroka(key);
    }

    virtual void OnMyEndMap() override
    {
        NodeStack.pop();
    }

    virtual void OnMyBeginAttributes() override
    {
        Y_ASSERT(!AttributeConsumer);
        Attributes = CreateEphemeralAttributes();
        AttributeConsumer.reset(new TAttributeConsumer(Attributes.get()));
        Forward(AttributeConsumer.get(), TClosure(), NYson::EYsonType::MapFragment);
    }

    virtual void OnMyEndAttributes() override
    {
        AttributeConsumer.reset();
        Y_ASSERT(Attributes);
    }

private:
    INodeFactoryPtr Factory;
    //! Contains nodes forming the current path in the tree.
    std::stack<INodePtr> NodeStack;
    TNullable<Stroka> Key;
    INodePtr ResultNode;
    std::unique_ptr<TAttributeConsumer> AttributeConsumer;
    std::unique_ptr<IAttributeDictionary> Attributes;

    void AddNode(INodePtr node, bool push)
    {
        if (Attributes) {
            node->MutableAttributes()->MergeFrom(*Attributes);
            Attributes.reset();
        }

        if (NodeStack.empty()) {
            ResultNode = node;
        } else {
            auto collectionNode = NodeStack.top();
            if (Key) {
                if (!collectionNode->AsMap()->AddChild(node, *Key)) {
                    THROW_ERROR_EXCEPTION("Duplicate key %Qv", *Key);
                }
                Key.Reset();
            } else {
                collectionNode->AsList()->AddChild(node);
            }
        }

        if (push) {
            NodeStack.push(node);
        }
    }
};

std::unique_ptr<ITreeBuilder> CreateBuilderFromFactory(INodeFactoryPtr factory)
{
    return std::unique_ptr<ITreeBuilder>(new TTreeBuilder(factory));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
