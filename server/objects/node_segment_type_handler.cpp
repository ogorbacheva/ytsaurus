#include "node_segment_type_handler.h"
#include "type_handler_detail.h"
#include "node_segment.h"
#include "pod_set.h"
#include "db_schema.h"

namespace NYP::NServer::NObjects {

////////////////////////////////////////////////////////////////////////////////

class TNodeSegmentTypeHandler
    : public TObjectTypeHandlerBase
{
public:
    explicit TNodeSegmentTypeHandler(NMaster::TBootstrap* bootstrap)
        : TObjectTypeHandlerBase(bootstrap, EObjectType::NodeSegment)
    {
        SpecAttributeSchema_
            ->SetAttribute(TNodeSegment::SpecSchema);

        StatusAttributeSchema_
            ->SetAttribute(TNodeSegment::StatusSchema);
    }

    virtual const NYson::TProtobufMessageType* GetRootProtobufType() override
    {
        return NYson::ReflectProtobufMessageType<NClient::NApi::NProto::TNodeSegment>();
    }

    virtual const TDBTable* GetTable() override
    {
        return &NodeSegmentsTable;
    }

    virtual const TDBField* GetIdField() override
    {
        return &NodeSegmentsTable.Fields.Meta_Id;
    }

    virtual std::unique_ptr<TObject> InstantiateObject(
        const TObjectId& id,
        const TObjectId& parentId,
        ISession* session) override
    {
        YCHECK(!parentId);
        return std::unique_ptr<TObject>(new TNodeSegment(id, this, session));
    }

    virtual void BeforeObjectRemoved(
        TTransaction* transaction,
        TObject* object) override
    {
        TObjectTypeHandlerBase::BeforeObjectRemoved(transaction, object);

        auto* segment = object->As<TNodeSegment>();
        const auto& podSets = segment->PodSets().Load();
        if (!podSets.empty()) {
            THROW_ERROR_EXCEPTION("Cannot remove node segment %Qv since it has %v pod set(s) assigned",
                segment->GetId(),
                podSets.size());
        }
    }
};

std::unique_ptr<IObjectTypeHandler> CreateNodeSegmentTypeHandler(NMaster::TBootstrap* bootstrap)
{
    return std::unique_ptr<IObjectTypeHandler>(new TNodeSegmentTypeHandler(bootstrap));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NObjects

