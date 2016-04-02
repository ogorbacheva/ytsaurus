#pragma once

#include <yt/server/hydra/public.h>

#include <yt/ytlib/cypress_client/public.h>

#include <yt/ytlib/object_client/public.h>

#include <yt/core/misc/public.h>
#include <yt/core/misc/small_vector.h>

namespace NYT {
namespace NCypressServer {

////////////////////////////////////////////////////////////////////////////////

using NCypressClient::TNodeId;
using NCypressClient::TLockId;
using NCypressClient::ELockMode;
using NCypressClient::ELockState;
using NCypressClient::TVersionedNodeId;

using NObjectClient::TTransactionId;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(INodeTypeHandler)
DECLARE_REFCOUNTED_STRUCT(ICypressNodeProxy)
DECLARE_REFCOUNTED_STRUCT(ICypressNodeVisitor)

DECLARE_REFCOUNTED_CLASS(TAccessTracker)
DECLARE_REFCOUNTED_CLASS(TCypressManager)

struct ICypressNodeFactory;

DECLARE_ENTITY_TYPE(TCypressNodeBase, TVersionedNodeId, NObjectClient::TDirectVersionedObjectIdHash)
DECLARE_ENTITY_TYPE(TLock, TLockId, NObjectClient::TDirectObjectIdHash)

using TCypressNodeList = SmallVector<TCypressNodeBase*, 8>;

struct TLockRequest;

DECLARE_REFCOUNTED_CLASS(TCypressManagerConfig)

////////////////////////////////////////////////////////////////////////////////

//! Describes the reason for cloning a node.
//! Some node types may allow moving but not copying.
DEFINE_ENUM(ENodeCloneMode,
    (Copy)
    (Move)
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressServer
} // namespace NYT
