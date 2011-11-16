#pragma once

#include "common.h"
#include "ypath_service.h"
#include "ytree.h"

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TVirtualMapBase
    : public IYPathService
{
protected:
    virtual yvector<Stroka> GetKeys() = 0;
    virtual IYPathService::TPtr GetItemService(const Stroka& key) = 0;

private:
    virtual TResolveResult Resolve(TYPath path, bool mustExist);
    virtual void Invoke(NRpc::IServiceContext* context);

};

////////////////////////////////////////////////////////////////////////////////

INode::TPtr CreateVirtualNode(
    TYPathServiceProducer* builder,
    INodeFactory* factory);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT

