#include "stdafx.h"
#include "monitoring_manager.h"

#include "../ytree/ephemeral.h"
#include "../ytree/yson_writer.h"
#include "../ytree/tree_visitor.h"
#include "../ytree/ypath_rpc.h"
#include "../actions/action_util.h"
#include "../misc/assert.h"

#include "stat.h"

namespace NYT {
namespace NMonitoring {

using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger Logger("Monitoring");
const TDuration TMonitoringManager::Period = TDuration::Seconds(3);

////////////////////////////////////////////////////////////////////////////////

TMonitoringManager::TMonitoringManager()
    : IsStarted(false)
{
    PeriodicInvoker = New<TPeriodicInvoker>(
        FromMethod(&TMonitoringManager::Update, TPtr(this)),
        Period);
}

void TMonitoringManager::Register(const TYPath& path, TYsonProducer::TPtr producer)
{
    TGuard<TSpinLock> guard(SpinLock);
    YVERIFY(MonitoringMap.insert(MakePair(path, producer)).Second());
}

void TMonitoringManager::Unregister(const TYPath& path)
{
    TGuard<TSpinLock> guard(SpinLock);
    YVERIFY(MonitoringMap.erase(Stroka(path)));
}

INode::TPtr TMonitoringManager::GetRoot() const
{
    return Root;
}

void TMonitoringManager::Start()
{
    YASSERT(!IsStarted);

    IsStarted = true;
    // Update the root right away to prevent GetRoot from returning NULL.
    Update();
    PeriodicInvoker->Start();
}

void TMonitoringManager::Stop()
{
    if (!IsStarted)
        return;

    IsStarted = false;
    PeriodicInvoker->Stop();
    Root.Reset();
}

void TMonitoringManager::Update()
{
    try {
        TIMEIT("stateman.updatetime", "tv",

        auto newRoot = GetEphemeralNodeFactory()->CreateMap();
        auto newRootService = IYPathService::FromNode(~newRoot);

        FOREACH(const auto& pair, MonitoringMap) {
            TStringStream output;
            TYsonWriter writer(&output, TYsonWriter::EFormat::Binary);
            pair.second->Do(&writer);

            SyncYPathSet(~newRootService, pair.first, output.Str());
        }

        if (IsStarted) {
            Root = newRoot;
        }

        )

    } catch (...) {
        LOG_FATAL("Error collecting monitoring data\n%s",
            ~CurrentExceptionMessage());
    }
}

void TMonitoringManager::Visit(IYsonConsumer* consumer)
{
    TIMEIT("stateman.visittime", "tv",

    TTreeVisitor visitor(consumer);
    visitor.Visit(~GetRoot());

    )
}

TYsonProducer::TPtr TMonitoringManager::GetProducer()
{
    YASSERT(IsStarted);
    YASSERT(Root);

    return FromMethod(&TMonitoringManager::Visit, TPtr(this));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMonitoring
} // namespace NYT
