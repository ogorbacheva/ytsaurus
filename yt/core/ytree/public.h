#pragma once

#include <yt/core/misc/public.h>

#include <yt/core/ypath/public.h>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

class TAttributeFilter;

} // namespace NProto

class TYsonSerializableLite;
class TYsonSerializable;

DECLARE_REFCOUNTED_STRUCT(INode);
typedef TIntrusivePtr<const INode> IConstNodePtr;
DECLARE_REFCOUNTED_STRUCT(ICompositeNode);
typedef TIntrusivePtr<ICompositeNode> ICompositeNodePtr;
DECLARE_REFCOUNTED_STRUCT(IStringNode);
DECLARE_REFCOUNTED_STRUCT(IInt64Node);
DECLARE_REFCOUNTED_STRUCT(IUint64Node);
DECLARE_REFCOUNTED_STRUCT(IDoubleNode);
DECLARE_REFCOUNTED_STRUCT(IBooleanNode);
DECLARE_REFCOUNTED_STRUCT(IListNode);
DECLARE_REFCOUNTED_STRUCT(IMapNode);
DECLARE_REFCOUNTED_STRUCT(IEntityNode);

DECLARE_REFCOUNTED_STRUCT(INodeFactory);
DECLARE_REFCOUNTED_STRUCT(INodeResolver);

struct IAttributeDictionary;

struct IAttributeOwner;

struct TAttributeFilter;

struct ISystemAttributeProvider;

DECLARE_REFCOUNTED_STRUCT(IYPathService);

DECLARE_REFCOUNTED_CLASS(TYPathRequest);
DECLARE_REFCOUNTED_CLASS(TYPathResponse);

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathRequest;

template <class TRequestMessage, class TResponseMessage>
class TTypedYPathResponse;

using NYPath::TYPath;

//! Default limit for List and Get requests to virtual nodes.
const i64 DefaultVirtualChildLimit = 1000;

////////////////////////////////////////////////////////////////////////////////

//! A static node type.
DEFINE_ENUM(ENodeType,
    // Node contains a string (Stroka).
    (String)
    // Node contains an int64 number (i64).
    (Int64)
    // Node contains an uint64 number (ui64).
    (Uint64)
    // Node contains an FP number (double).
    (Double)
    // Node contains an boolean (bool).
    (Boolean)
    // Node contains a map from strings to other nodes.
    (Map)
    // Node contains a list (vector) of other nodes.
    (List)
    // Node is atomic, i.e. has no visible properties (aside from attributes).
    (Entity)
    // Either List or Map.
    (Composite)
);

DEFINE_ENUM(EErrorCode,
    ((ResolveError)    (500))
    ((AlreadyExists)   (501))
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
