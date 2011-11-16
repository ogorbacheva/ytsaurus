#pragma once

#include "common.h"
#include "yson_events.h"

#include "../misc/property.h"
#include "../rpc/service.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

DECLARE_POLY_ENUM2(EYPathErrorCode, NRpc::EErrorCode,
    ((NoSuchVerb)(100))
    ((GenericError)(101))
);

////////////////////////////////////////////////////////////////////////////////

struct IYPathService
    : virtual TRefCountedBase
{
    typedef TIntrusivePtr<IYPathService> TPtr;

    class TResolveResult
    {
        DECLARE_BYVAL_RO_PROPERTY(Service, IYPathService::TPtr);
        DECLARE_BYVAL_RO_PROPERTY(Path, TYPath);

    public:
        static TResolveResult Here(TYPath path)
        {
            TResolveResult result;
            result.Path_ = path;
            return result;
        }

        static TResolveResult There(IYPathService* service, TYPath path)
        {
            TResolveResult result;
            result.Service_ = service;
            result.Path_ = path;
            return result;
        }

        bool IsHere() const
        {
            return ~Service_ == NULL;
        }
    };

    virtual TResolveResult Resolve(TYPath path, bool mustExist) = 0;
    virtual void Invoke(NRpc::IServiceContext* context) = 0;

    static IYPathService::TPtr FromNode(INode* node);
    static IYPathService::TPtr FromProducer(TYsonProducer* producer);
};

typedef IFunc<NYTree::IYPathService::TPtr> TYPathServiceProducer;

////////////////////////////////////////////////////////////////////////////////


} // namespace NYTree
} // namespace NYT
