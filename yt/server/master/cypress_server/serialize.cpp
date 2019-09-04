#include "serialize.h"

#include <yt/server/master/security_server/security_manager.h>

#include <yt/server/master/chunk_server/chunk_manager.h>

#include <yt/server/master/tablet_server/tablet_manager.h>

#include <yt/server/master/cell_master/bootstrap.h>

namespace NYT::NCypressServer {

using namespace NSecurityServer;
using namespace NObjectServer;
using namespace NChunkServer;
using namespace NObjectServer;
using namespace NTabletServer;
using namespace NTableServer;
using namespace NTransactionServer;

////////////////////////////////////////////////////////////////////////////////

TBeginCopyContext::TBeginCopyContext(
    TTransaction* transaction,
    bool removeSource)
    : Transaction_(transaction)
    , RemoveSource_(removeSource)
    , TableSchemaRegistry_(New<TTableSchemaRegistry>())
    , Stream_(Data_)
{
    SetOutput(&Stream_);
}

TString TBeginCopyContext::Finish()
{
    return std::move(Data_);
}

TCellTagList TBeginCopyContext::GetExternalCellTags()
{
    SortUnique(ExternalCellTags_);
    return TCellTagList(ExternalCellTags_.begin(), ExternalCellTags_.end());
}

void TBeginCopyContext::RegisterOpaqueRootId(TNodeId rootId)
{
    OpaqueRootIds_.push_back(rootId);
}

void TBeginCopyContext::RegisterExternalCellTag(TCellTag cellTag)
{
    ExternalCellTags_.push_back(cellTag);
}

const TTableSchemaRegistryPtr& TBeginCopyContext::GetTableSchemaRegistry() const
{
    return TableSchemaRegistry_;
}

////////////////////////////////////////////////////////////////////////////////

TEndCopyContext::TEndCopyContext(
    NCellMaster::TBootstrap* bootstrap,
    ENodeCloneMode mode,
    TRef data)
    : Mode_(mode)
    , Bootstrap_(bootstrap)
    , TableSchemaRegistry_(New<NTableServer::TTableSchemaRegistry>())
    , Stream_(data.Begin(), data.Size())
{
    SetInput(&Stream_);
}

template <>
TSubject* TEndCopyContext::GetObject(TObjectId id)
{
    return Bootstrap_->GetSecurityManager()->GetSubjectOrThrow(id);
}

template <>
TAccount* TEndCopyContext::GetObject(TObjectId id)
{
    return Bootstrap_->GetSecurityManager()->GetAccountOrThrow(id);
}

template <>
TMedium* TEndCopyContext::GetObject(TObjectId id)
{
    return Bootstrap_->GetChunkManager()->GetMediumOrThrow(id);
}

template <>
TTabletCellBundle* TEndCopyContext::GetObject(TObjectId id)
{
    return Bootstrap_->GetTabletManager()->GetTabletCellBundleOrThrow(id);
}

template <>
const TSecurityTagsRegistryPtr& TEndCopyContext::GetInternRegistry() const
{
    const auto& securityManager = Bootstrap_->GetSecurityManager();
    return securityManager->GetSecurityTagsRegistry();
}

template <>
const TTableSchemaRegistryPtr& TEndCopyContext::GetInternRegistry() const
{
    return TableSchemaRegistry_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressServer
