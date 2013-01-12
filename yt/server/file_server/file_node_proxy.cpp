#include "stdafx.h"
#include "file_node_proxy.h"
#include "file_node.h"

#include <ytlib/misc/string.h>

#include <ytlib/chunk_client/chunk_meta_extensions.h>
#include <ytlib/chunk_client/chunk.pb.h>

#include <server/chunk_server/chunk.h>
#include <server/chunk_server/chunk_list.h>
#include <server/chunk_server/chunk_manager.h>

#include <server/cypress_server/node_proxy_detail.h>

namespace NYT {
namespace NFileServer {

using namespace NChunkServer;
using namespace NCypressServer;
using namespace NYTree;
using namespace NYson;
using namespace NRpc;
using namespace NObjectServer;
using namespace NCellMaster;
using namespace NTransactionServer;
using namespace NSecurityServer;
using namespace NFileClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

class TFileNodeProxy
    : public TCypressNodeProxyBase<TCypressNodeProxyNontemplateBase, IEntityNode, TFileNode>
{
public:
    TFileNodeProxy(
        INodeTypeHandlerPtr typeHandler,
        NCellMaster::TBootstrap* bootstrap,
        TTransaction* transaction,
        TFileNode* trunkNode)
        : TBase(
            typeHandler,
            bootstrap,
            transaction,
            trunkNode)
    { }

    virtual bool IsWriteRequest(NRpc::IServiceContextPtr context) const override
    {
        DECLARE_YPATH_SERVICE_WRITE_METHOD(PrepareForUpdate);
        return TBase::IsWriteRequest(context);
    }

    virtual NSecurityServer::TClusterResources GetResourceUsage() const override
    {
        const auto* node = GetThisTypedImpl();
        const auto* chunkList = node->GetChunkList();
        i64 diskSpace = chunkList->Statistics().DiskSpace * node->GetReplicationFactor();
        return TClusterResources::FromDiskSpace(diskSpace);
    }

private:
    typedef NCypressServer::TCypressNodeProxyBase<TCypressNodeProxyNontemplateBase, IEntityNode, TFileNode> TBase;

    virtual void ListSystemAttributes(std::vector<TAttributeInfo>* attributes) const override
    {
        const auto* node = GetThisTypedImpl();
        const auto* chunkList = node->GetChunkList();
        bool hasChunk = !chunkList->Children().empty();

        attributes->push_back(TAttributeInfo("size", hasChunk));
        attributes->push_back(TAttributeInfo("compressed_size", hasChunk));
        attributes->push_back(TAttributeInfo("compression_ratio", hasChunk));
        attributes->push_back(TAttributeInfo("codec", hasChunk));
        attributes->push_back("chunk_list_id");
        attributes->push_back(TAttributeInfo("chunk_id", hasChunk));
        attributes->push_back("replication_factor");
        TBase::ListSystemAttributes(attributes);
    }

    virtual bool GetSystemAttribute(const Stroka& key, IYsonConsumer* consumer) const override
    {
        const auto* node = GetThisTypedImpl();
        const auto* chunkList = node->GetChunkList();
        const auto& statistics = chunkList->Statistics();
        YCHECK(chunkList->Children().size() <= 1);
        const auto* chunk = chunkList->Children().empty() ? nullptr : chunkList->Children()[0].AsChunk();

        if (chunk) {
            auto miscExt = GetProtoExtension<TMiscExt>(chunk->ChunkMeta().extensions());

            if (key == "size") {
                BuildYsonFluently(consumer)
                    .Value(statistics.UncompressedDataSize);
                return true;
            }

            if (key == "compressed_size") {
                BuildYsonFluently(consumer)
                    .Value(statistics.CompressedDataSize);
                return true;
            }

            if (key == "compression_ratio") {
                double ratio = statistics.UncompressedDataSize > 0 ?
                    static_cast<double>(statistics.CompressedDataSize) / statistics.UncompressedDataSize : 0;
                BuildYsonFluently(consumer)
                    .Value(ratio);
                return true;
            }

            if (key == "codec") {
                auto codecId = ECodec(miscExt.codec());
                BuildYsonFluently(consumer)
                    .Value(CamelCaseToUnderscoreCase(codecId.ToString()));
                return true;
            }

            if (key == "chunk_id") {
                BuildYsonFluently(consumer)
                    .Value(chunk->GetId().ToString());
                return true;
            }
        }

        if (key == "chunk_list_id") {
            BuildYsonFluently(consumer)
                .Value(chunkList->GetId().ToString());
            return true;
        }

        if (key == "replication_factor") {
            BuildYsonFluently(consumer)
                .Value(node->GetReplicationFactor());
            return true;
        }

        return TBase::GetSystemAttribute(key, consumer);
    }

    virtual void ValidateUserAttributeUpdate(
        const Stroka& key,
        const TNullable<TYsonString>& oldValue,
        const TNullable<TYsonString>& newValue) override
    {
        UNUSED(oldValue);

        if (key == "executable" && newValue) {
            ConvertTo<bool>(*newValue);
            return;
        }

        if (key == "file_name" && newValue) {
            // File name must be string.
            // ToDo(psushin): write more sophisticated validation.
            ConvertTo<Stroka>(*newValue);
            return;
        }
    }

    virtual bool SetSystemAttribute(const Stroka& key, const TYsonString& value) override
    {
        auto chunkManager = Bootstrap->GetChunkManager();

        if (key == "replication_factor") {
            if (Transaction) {
                THROW_ERROR_EXCEPTION("Attribute cannot be altered inside transaction");
            }

            int replicationFactor = ConvertTo<int>(value);
            const int MinReplicationFactor = 1;
            const int MaxReplicationFactor = 10;
            if (replicationFactor < MinReplicationFactor || replicationFactor > MaxReplicationFactor) {
                THROW_ERROR_EXCEPTION("Value must be in range [%d,%d]",
                    MinReplicationFactor,
                    MaxReplicationFactor);
            }

            auto* node = GetThisTypedMutableImpl();
            YCHECK(node->IsTrunk());

            if (node->GetReplicationFactor() != replicationFactor) {
                node->SetReplicationFactor(replicationFactor);

                auto securityManager = Bootstrap->GetSecurityManager();
                securityManager->UpdateAccountNodeUsage(node);

                if (IsLeader()) {
                    chunkManager->ScheduleRFUpdate(node->GetChunkList());
                }
            }

            return true;
        }

        return TBase::SetSystemAttribute(key, value);
    }

    virtual void DoInvoke(NRpc::IServiceContextPtr context) override
    {
        DISPATCH_YPATH_SERVICE_METHOD(FetchFile);
        DISPATCH_YPATH_SERVICE_METHOD(PrepareForUpdate);
        TBase::DoInvoke(context);
    }

    bool IsExecutable()
    {
        return Attributes().Get("executable", false);
    }

    Stroka GetFileName()
    {
        // TODO(ignat): Remake wrapper and than delete this option
        auto fileName = Attributes().Find<Stroka>("file_name");
        if (fileName) {
            return *fileName;
        }

        auto parent = GetParent();
        YASSERT(parent);

        switch (parent->GetType()) {
        case ENodeType::Map:
            return parent->AsMap()->GetChildKey(this);

        case ENodeType::List:
            return ToString(parent->AsList()->GetChildIndex(this));

        default:
            YUNREACHABLE();
        }
    }

    DECLARE_RPC_SERVICE_METHOD(NFileClient::NProto, FetchFile)
    {
        UNUSED(request);

        auto chunkManager = Bootstrap->GetChunkManager();

        const auto* node = GetThisTypedImpl();

        const auto* chunkList = node->GetChunkList();
        YCHECK(chunkList->Children().size() <= 1);
        if (chunkList->Children().size() == 0) {
            THROW_ERROR_EXCEPTION("No chunk is associated with the file");
        }

        auto chunkRef = chunkList->Children()[0];
        YCHECK(chunkRef.GetType() == EObjectType::Chunk);

        auto chunkId = chunkRef.GetId();
        const auto* chunk = chunkRef.AsChunk();

        *response->mutable_chunk_id() = chunkId.ToProto();
        auto addresses = chunkManager->GetChunkAddresses(chunk);
        FOREACH (const auto& address, addresses) {
            response->add_node_addresses(address);
        }

        response->set_executable(IsExecutable());
        response->set_file_name(GetFileName());

        context->SetResponseInfo("ChunkId: %s, FileName: %s, Executable: %s, Addresses: [%s]",
            ~chunkId.ToString(),
            ~response->file_name(),
            ~ToString(response->executable()),
            ~JoinToString(addresses));

        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NFileClient::NProto, PrepareForUpdate)
    {
        context->SetRequestInfo("");

        if (!Transaction) {
            THROW_ERROR_EXCEPTION("Transaction required");
        }

        auto* node = LockThisTypedImpl();

        if (node->GetUpdateMode() != EFileUpdateMode::None) {
            THROW_ERROR_EXCEPTION("Node is already in %s mode",
                ~FormatEnum(node->GetUpdateMode()).Quote());
        }

        auto chunkManager = Bootstrap->GetChunkManager();
        auto objectManager = Bootstrap->GetObjectManager();

        auto* oldChunkList = node->GetChunkList();
        YCHECK(oldChunkList->OwningNodes().erase(node) == 1);
        objectManager->UnrefObject(oldChunkList);

        auto* newChunkList = chunkManager->CreateChunkList();
        YCHECK(newChunkList->OwningNodes().insert(node).second);
        node->SetChunkList(newChunkList);
        objectManager->RefObject(newChunkList);

        LOG_DEBUG_UNLESS(IsRecovery(), "File node is switched to \"overwrite\" mode (NodeId: %s, NewChunkListId: %s)",
            ~node->GetId().ToString(),
            ~newChunkList->GetId().ToString());

        node->SetUpdateMode(EFileUpdateMode::Overwrite);

        SetModified();

        *response->mutable_chunk_list_id() = newChunkList->GetId().ToProto();
        context->SetResponseInfo("ChunkListId: %s", ~newChunkList->GetId().ToString());

        context->Reply();
    }

};

////////////////////////////////////////////////////////////////////////////////

ICypressNodeProxyPtr CreateFileNodeProxy(
    NCypressServer::INodeTypeHandlerPtr typeHandler,
    NCellMaster::TBootstrap* bootstrap,
    NTransactionServer::TTransaction* transaction,
    TFileNode* trunkNode)
{

    return New<TFileNodeProxy>(
        typeHandler,
        bootstrap,
        transaction,
        trunkNode);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileServer
} // namespace NYT

