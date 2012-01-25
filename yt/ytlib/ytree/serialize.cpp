#include "ytree.h"
#include "yson_reader.h"
#include "tree_visitor.h"
#include "tree_builder.h"
#include "serialize.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

TYsonProducer::TPtr ProducerFromYson(TInputStream* input)
{
    return FromFunctor([=] (IYsonConsumer* consumer)
        {
            TYsonReader reader(consumer, input);
            reader.Read();
        });
}

TYsonProducer::TPtr ProducerFromYson(const TYson& data)
{
    return FromFunctor([=] (IYsonConsumer* consumer)
        {
            TStringInput input(data);
            TYsonReader reader(consumer, &input);
            reader.Read();
        });
}

TYsonProducer::TPtr ProducerFromNode(const INode* node)
{
    return FromFunctor([=] (IYsonConsumer* consumer)
        {
            TTreeVisitor visitor(consumer);
            visitor.Visit(node);
        });
}

INode::TPtr DeserializeFromYson(TInputStream* input, INodeFactory* factory)
{
    auto builder = CreateBuilderFromFactory(factory);
    builder->BeginTree();
    TYsonReader reader(~builder, input);
    reader.Read();
    return builder->EndTree();
}

INode::TPtr DeserializeFromYson(const TYson& yson, INodeFactory* factory)
{
    TStringInput input(yson);
    return DeserializeFromYson(&input, factory);
}

TOutputStream& SerializeToYson(
    const INode* node,
    TOutputStream& output,
    EYsonFormat format)
{
    TYsonWriter writer(&output, format);
    TTreeVisitor visitor(&writer);
    visitor.Visit(node);
    return output;
}

TYson SerializeToYson(const INode* node, EYsonFormat format)
{
    TStringStream output;
    SerializeToYson(node, output, format);
    return output.Str();
}

TYson SerializeToYson(TYsonProducer* producer, EYsonFormat format)
{
    TStringStream output;
    TYsonWriter writer(&output, format);
    producer->Do(&writer);
    return output.Str();
}

TYson SerializeToYson(const TConfigurable* config, EYsonFormat format)
{
    TStringStream output;
    TYsonWriter writer(&output, format);
    config->Save(&writer);
    return output.Str();
}

INode::TPtr CloneNode(const INode* node, INodeFactory* factory)
{
    auto builder = CreateBuilderFromFactory(factory);
    builder->BeginTree();
    TTreeVisitor visitor(~builder);
    visitor.Visit(node);
    return builder->EndTree();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
