#include "stdafx.h"
#include "redirector_service.h"
#include "scheduler_proxy.h"

#include <ytlib/ytree/ypath_client.h>
#include <ytlib/ytree/serialize.h>
#include <ytlib/cell_master/bootstrap.h>
#include <ytlib/rpc/redirector_service_base.h>
#include <ytlib/cypress/cypress_manager.h>

namespace NYT {
namespace NScheduler {

using namespace NCypress;
using namespace NRpc;
using namespace NYTree;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger Logger("SchedulerRedirector");

////////////////////////////////////////////////////////////////////////////////

class TRedirectorService
    : public NRpc::TRedirectorServiceBase
{
public:
    typedef TIntrusivePtr<TRedirectorService> TPtr;

    TRedirectorService(NCellMaster::TBootstrap* bootstrap)
        : TRedirectorServiceBase(TSchedulerServiceProxy::GetServiceName(), Logger.GetCategory())
        , Bootstrap(bootstrap)
    { }

protected:
    TBootstrap* Bootstrap;

    virtual TAsyncRedirectResult HandleRedirect(NRpc::IServiceContext* context)
    {
        return 
            FromMethod(&TRedirectorService::DoHandleRedirect, MakeStrong(this))
            ->AsyncVia(Bootstrap->GetStateInvoker())
            ->Do(context);
    }

    TRedirectResult DoHandleRedirect(NRpc::IServiceContext::TPtr context)
    {
        if (Bootstrap->GetMetaStateManager()->GetStateStatus() != NMetaState::EPeerStatus::Leading) {
            return TError("Not a leader");
        }

        auto cypressManager = Bootstrap->GetCypressManager();
        auto root = cypressManager->GetVersionedNodeProxy(cypressManager->GetRootNodeId(), NullTransactionId);

        TRedirectParams redirectParams;
        try {
            redirectParams.Address = DeserializeFromYson<Stroka>(SyncYPathGet(~root, "sys/scheduler/runtime@address"));
        } catch (const std::exception& ex) {
            return TError("Error reading redirection parameters\n%s", ex.what());
        }

        return redirectParams;
    }

};

IService::TPtr CreateRedirectorService(TBootstrap* bootstrap)
{
    return New<TRedirectorService>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
