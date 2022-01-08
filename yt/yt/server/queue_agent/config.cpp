#include "config.h"

#include <yt/yt/ytlib/api/native/config.h>

#include <yt/yt/client/security_client/public.h>

#include <yt/yt/core/ytree/ephemeral_node_factory.h>

#include <yt/yt/library/re2/re2.h>

namespace NYT::NQueueAgent {

using namespace NSecurityClient;

////////////////////////////////////////////////////////////////////////////////

void TQueueAgentConfig::Register(TRegistrar registrar)
{

    registrar.Parameter("poll_period", &TThis::PollPeriod)
        .Default(TDuration::Seconds(1));
    registrar.Parameter("controller_thread_count", &TThis::ControllerThreadCount)
        .Default(4);
}

////////////////////////////////////////////////////////////////////////////////

void TQueueAgentServerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("cluster_connection", &TThis::ClusterConnection);

    registrar.Parameter("queue_agent", &TThis::QueueAgent)
        .DefaultNew();
    registrar.Parameter("abort_on_unrecognized_options", &TThis::AbortOnUnrecognizedOptions)
        .Default(false);
    registrar.Parameter("user", &TThis::User)
        .Default(QueueAgentUserName);
    registrar.Parameter("cypress_annotations", &TThis::CypressAnnotations)
        .Default(NYTree::BuildYsonNodeFluently()
            .BeginMap()
            .EndMap()
        ->AsMap());
    registrar.Parameter("root", &TThis::Root)
        .Default("//sys/queue_agents");
    registrar.Parameter("election_manager", &TThis::ElectionManager)
        .DefaultNew();

    registrar.Postprocessor([] (TThis* config) {
        if (auto& lockPath = config->ElectionManager->LockPath; lockPath.empty()) {
            lockPath = config->Root + "/leader_lock";
        }
    });
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueueAgent
