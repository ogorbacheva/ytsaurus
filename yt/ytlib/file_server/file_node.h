#pragma once

#include "common.h"

#include "../misc/property.h"
#include "../chunk_server/chunk_manager.h"
#include "../cypress/node_detail.h"

namespace NYT {
namespace NFileServer {

using namespace NCypress;
using NChunkServer::TChunkListId;
using NChunkServer::NullChunkListId;

////////////////////////////////////////////////////////////////////////////////

class TFileNode
    : public NCypress::TCypressNodeBase
{
    DECLARE_BYVAL_RW_PROPERTY(ChunkListId, TChunkListId);

public:
    explicit TFileNode(const TBranchedNodeId& id);
    TFileNode(const TBranchedNodeId& id, const TFileNode& other);

    virtual TAutoPtr<ICypressNode> Clone() const;

    virtual ERuntimeNodeType GetRuntimeType() const;

};

////////////////////////////////////////////////////////////////////////////////

class TFileManager;

class TFileNodeTypeHandler
    : public TCypressNodeTypeHandlerBase<TFileNode>
{
public:
    TFileNodeTypeHandler(
        TCypressManager* cypressManager,
        TFileManager* fileManager,
        NChunkServer::TChunkManager* chunkManager);

    ERuntimeNodeType GetRuntimeType();

    Stroka GetTypeName();

    virtual TAutoPtr<ICypressNode> Create(
        const TNodeId& nodeId,
        const TTransactionId& transactionId,
        IMapNode::TPtr manifest);

    virtual TIntrusivePtr<ICypressNodeProxy> GetProxy(
        const ICypressNode& node,
        const TTransactionId& transactionId);

protected:
    virtual void DoDestroy(TFileNode& node);

    virtual void DoBranch(
        const TFileNode& committedNode,
        TFileNode& branchedNode);

    virtual void DoMerge(
        TFileNode& committedNode,
        TFileNode& branchedNode);

private:
    typedef TFileNodeTypeHandler TThis;

    TIntrusivePtr<TFileManager> FileManager;
    TIntrusivePtr<NChunkServer::TChunkManager> ChunkManager;

    static void GetSize(const TGetAttributeRequest& request);
    static void GetChunkListId(const TGetAttributeRequest& request);
    static void GetChunkId(const TGetAttributeRequest& request);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileServer
} // namespace NYT

