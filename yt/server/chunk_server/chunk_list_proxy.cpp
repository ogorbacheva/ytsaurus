#include "stdafx.h"
#include "chunk_list_proxy.h"
#include "private.h"
#include "chunk_list.h"
#include "chunk_manager.h"

#include <ytlib/chunk_client/chunk_list_ypath.pb.h>

#include <server/cell_master/bootstrap.h>

#include <server/object_server/object_detail.h>

namespace NYT {
namespace NChunkServer {

using namespace NYTree;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

class TChunkListProxy
    : public TNonversionedObjectProxyBase<TChunkList>
{
public:
    TChunkListProxy(NCellMaster::TBootstrap* bootstrap, TChunkList* chunkList)
        : TBase(bootstrap, chunkList)
    {
        Logger = ChunkServerLogger;
    }

    virtual bool IsWriteRequest(NRpc::IServiceContextPtr context) const override
    {
        DECLARE_YPATH_SERVICE_WRITE_METHOD(Attach);
        return TBase::IsWriteRequest(context);
    }

private:
    typedef TNonversionedObjectProxyBase<TChunkList> TBase;

    virtual void ListSystemAttributes(std::vector<TAttributeInfo>* attributes) override
    {
        attributes->push_back("children_ids");
        attributes->push_back("parent_ids");
        attributes->push_back("statistics");
        attributes->push_back(TAttributeInfo("tree", true, true));
        attributes->push_back(TAttributeInfo("owning_nodes", true, true));
        TBase::ListSystemAttributes(attributes);
    }

    void TraverseTree(TChunkTree* chunkTree, NYson::IYsonConsumer* consumer)
    {
        switch (chunkTree->GetType()) {
            case EObjectType::Chunk: {
                consumer->OnStringScalar(ToString(chunkTree->GetId()));
                break;
            }

            case EObjectType::ChunkList: {
                const auto* chunkList = chunkTree->AsChunkList();
                consumer->OnBeginAttributes();
                consumer->OnKeyedItem("id");
                consumer->OnStringScalar(ToString(chunkList->GetId()));
                consumer->OnKeyedItem("rank");
                consumer->OnIntegerScalar(chunkList->Statistics().Rank);
                consumer->OnEndAttributes();

                consumer->OnBeginList();
                FOREACH (auto* child, chunkList->Children()) {
                    consumer->OnListItem();
                    TraverseTree(child, consumer);
                }
                consumer->OnEndList();
                break;
            }

            default:
                YUNREACHABLE();
        }
    }

    virtual bool GetSystemAttribute(const Stroka& key, NYson::IYsonConsumer* consumer) override
    {
        auto chunkManager = Bootstrap->GetChunkManager();
        const auto* chunkList = GetThisTypedImpl();

        if (key == "children_ids") {
            BuildYsonFluently(consumer)
                .DoListFor(chunkList->Children(), [=] (TFluentList fluent, const TChunkTree* child) {
                    fluent.Item().Value(child->GetId());
            });
            return true;
        }

        if (key == "parent_ids") {
            BuildYsonFluently(consumer)
                .DoListFor(chunkList->Parents(), [=] (TFluentList fluent, const TChunkList* chunkList) {
                    fluent.Item().Value(chunkList->GetId());
            });
            return true;
        }

        const auto& statistics = chunkList->Statistics();

        if (key == "statistics") {
            BuildYsonFluently(consumer)
                .Value(statistics);
            return true;
        }

        if (key == "tree") {
            TraverseTree(const_cast<TChunkList*>(chunkList), consumer);
            return true;
        }

        if (key == "owning_nodes") {
            auto paths = chunkManager->GetOwningNodes(const_cast<TChunkList*>(chunkList));
            BuildYsonFluently(consumer)
                .Value(paths);
            return true;
        }

        return TBase::GetSystemAttribute(key, consumer);
    }

    virtual bool DoInvoke(NRpc::IServiceContextPtr context) override
    {
        DISPATCH_YPATH_SERVICE_METHOD(Attach);
        return TBase::DoInvoke(context);
    }

    DECLARE_RPC_SERVICE_METHOD(NChunkClient::NProto, Attach)
    {
        UNUSED(response);

        auto childrenIds = FromProto<TChunkTreeId>(request->children_ids());

        context->SetRequestInfo("Children: [%s]", ~JoinToString(childrenIds));

        auto objectManager = Bootstrap->GetObjectManager();
        auto chunkManager = Bootstrap->GetChunkManager();

        std::vector<TChunkTree*> children;
        children.reserve(childrenIds.size());
        FOREACH (const auto& childId, childrenIds) {
            auto* child = chunkManager->FindChunkTree(childId);
            if (!IsObjectAlive(child)) {
                THROW_ERROR_EXCEPTION("No such chunk tree: %s", ~ToString(childId));
            }
            children.push_back(child);
        }

        auto* chunkList = GetThisTypedImpl();
        chunkManager->AttachToChunkList(chunkList, children);

        context->Reply();
    }

};

IObjectProxyPtr CreateChunkListProxy(
    NCellMaster::TBootstrap* bootstrap,
    TChunkList* chunkList)
{
    return New<TChunkListProxy>(bootstrap, chunkList);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
