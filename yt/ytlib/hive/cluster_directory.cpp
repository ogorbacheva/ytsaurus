#include "cluster_directory.h"

#include <yt/ytlib/api/client.h>
#include <yt/ytlib/api/config.h>
#include <yt/ytlib/api/connection.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/ytree/convert.h>

namespace NYT {
namespace NHive {

using namespace NRpc;
using namespace NApi;
using namespace NObjectClient;
using namespace NYTree;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

TClusterDirectory::TClusterDirectory(IConnectionPtr selfConnection)
    : SelfConnection_(selfConnection)
    , SelfClient_(SelfConnection_->CreateClient(GetRootClientOptions()))
{ }

IConnectionPtr TClusterDirectory::GetConnection(TCellTag cellTag) const
{
    TGuard<TSpinLock> guard(Lock_);
    auto it = CellTagToCluster_.find(cellTag);
    return it == CellTagToCluster_.end() ? nullptr : it->second.Connection;
}

IConnectionPtr TClusterDirectory::GetConnectionOrThrow(TCellTag cellTag) const
{
    auto connection = GetConnection(cellTag);
    if (!connection) {
        THROW_ERROR_EXCEPTION("Cannot find cluster with cell tag %v", cellTag);
    }
    return connection;
}

IConnectionPtr TClusterDirectory::GetConnection(const Stroka& clusterName) const
{
    TGuard<TSpinLock> guard(Lock_);
    auto it = NameToCluster_.find(clusterName);
    return it == NameToCluster_.end() ? nullptr : it->second.Connection;
}

IConnectionPtr TClusterDirectory::GetConnectionOrThrow(const Stroka& clusterName) const
{
    auto connection = GetConnection(clusterName);
    if (!connection) {
        THROW_ERROR_EXCEPTION("Cannot find cluster with name %Qv", clusterName);
    }
    return connection;
}

std::vector<Stroka> TClusterDirectory::GetClusterNames() const
{
    TGuard<TSpinLock> guard(Lock_);
    return GetKeys(NameToCluster_);
}

void TClusterDirectory::RemoveCluster(const Stroka& clusterName)
{
    TGuard<TSpinLock> guard(Lock_);
    auto it = NameToCluster_.find(clusterName);
    if (it == NameToCluster_.end())
        return;
    auto cellTag = it->second.CellTag;
    NameToCluster_.erase(it);
    YCHECK(CellTagToCluster_.erase(cellTag) == 1);
}

void TClusterDirectory::UpdateCluster(
    const Stroka& clusterName,
    TConnectionConfigPtr config)
{
    auto addNewCluster = [&] (const TCluster& cluster) {
        if (CellTagToCluster_.find(cluster.CellTag) != CellTagToCluster_.end()) {
            THROW_ERROR_EXCEPTION("Duplicate cell tag %v", cluster.CellTag);
        }
        CellTagToCluster_[cluster.CellTag] = cluster;
        NameToCluster_[cluster.Name] = cluster;
    };

    auto it = NameToCluster_.find(clusterName);
    if (it == NameToCluster_.end()) {
        auto cluster = CreateCluster(clusterName, config);
        TGuard<TSpinLock> guard(Lock_);
        addNewCluster(cluster);
    } else if (!AreNodesEqual(ConvertToNode(*(it->second.Config)), ConvertToNode(*config))) {
        auto cluster = CreateCluster(clusterName, config);
        TGuard<TSpinLock> guard(Lock_);
        CellTagToCluster_.erase(it->second.CellTag);
        NameToCluster_.erase(it);
        addNewCluster(cluster);
    }
}

void TClusterDirectory::UpdateSelf()
{
    auto cluster = CreateSelfCluster();
    TGuard<TSpinLock> guard(Lock_);
    CellTagToCluster_[cluster.CellTag] = cluster;
}

TClusterDirectory::TCluster TClusterDirectory::CreateCluster(
    const Stroka& name,
    TConnectionConfigPtr config) const
{
    TCluster cluster;
    cluster.Name = name;
    cluster.CellTag = config->Master->CellTag;
    cluster.Config = config;
    cluster.Connection = CreateConnection(config);
    return cluster;
}

TClusterDirectory::TCluster TClusterDirectory::CreateSelfCluster() const
{
    TCluster cluster;
    cluster.Name = "";
    cluster.CellTag = SelfConnection_->GetConfig()->Master->CellTag;
    cluster.Config = SelfConnection_->GetConfig();
    cluster.Connection = SelfConnection_;
    return cluster;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NHive
} // namespace NYT

