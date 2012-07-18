#include "stdafx.h"
#include "load_context.h"
#include "bootstrap.h"

#include <ytlib/transaction_server/transaction_manager.h>
#include <ytlib/object_server/object_detail.h>
#include <ytlib/object_server/id.h>
#include <ytlib/chunk_server/chunk_list.h>
#include <ytlib/chunk_server/chunk_manager.h>
#include <ytlib/cypress_server/cypress_manager.h>

namespace NYT {
namespace NCellMaster {

using namespace NObjectServer;
using namespace NTransactionServer;
using namespace NChunkServer;
using namespace NCypressServer;

////////////////////////////////////////////////////////////////////////////////

TLoadContext::TLoadContext(TBootstrap *bootstrap)
    : Bootstrap_(bootstrap)
{ }

template <>
TTransaction* TLoadContext::Get(const TObjectId& id) const
{
    return Bootstrap_->GetTransactionManager()->GetTransaction(id);
}

template <>
TChunkList* TLoadContext::Get(const TObjectId& id) const
{
    return Bootstrap_->GetChunkManager()->GetChunkList(id);
}

template <>
TChunk* TLoadContext::Get(const TObjectId& id) const
{
    return Bootstrap_->GetChunkManager()->GetChunk(id);
}

template <>
TJob* TLoadContext::Get(const TObjectId& id) const
{
    return Bootstrap_->GetChunkManager()->GetJob(id);
}

template <>
ICypressNode* TLoadContext::Get(const TVersionedObjectId& id) const
{
    return Bootstrap_->GetCypressManager()->GetNode(id);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT
