#include "cluster_tracker.h"

#include "auth_token.h"
#include "format_helpers.h"
#include "guarded_ptr.h"
#include "logging_helpers.h"
#include "type_helpers.h"

#include <Common/Exception.h>

#include <Poco/Logger.h>

#include <common/logger_useful.h>

#include <util/string/cast.h>
#include <util/system/rwlock.h>

#include <memory>

namespace NYT {
namespace NClickHouse {

using namespace DB;

////////////////////////////////////////////////////////////////////////////////

class TClusterNodeTracker;

using TClusterNodeTrackerPtr = std::shared_ptr<TClusterNodeTracker>;

////////////////////////////////////////////////////////////////////////////////

// Event handler for cluster node directory

// We need that indirection because tracker holds reference to context object
// and do not control its lifetime

class TClusterDirectoryEventHandler
    : public NInterop::INodeEventHandler
{
private:
    TGuardedPtr<TClusterNodeTracker> Tracker;

public:
    TClusterDirectoryEventHandler(TClusterNodeTrackerPtr tracker)
        : Tracker(std::move(tracker))
    {}

    void OnUpdate(
        const TString& path,
        NInterop::TNodeRevision newRevision) override;

    void OnRemove(const TString& path) override;

    void OnError(
        const TString& path,
        const TString& errorMessage) override;

    void Detach();
};

using TClusterDirectoryEventHandlerPtr = std::shared_ptr<TClusterDirectoryEventHandler>;

////////////////////////////////////////////////////////////////////////////////

namespace NEphemeralNodes {

static TString ToNodeNameHint(const std::string& host, ui16 port)
{
    return ToString(host) + ':' + ::ToString(port);
}

static TString ToNodeContent(const std::string& host, ui16 port)
{
    return ToString(host) + ':' + ::ToString(port);
}

static TClusterNodeName ToClusterNodeName(const TString& content)
{
    TStringBuf host;
    TStringBuf port;
    TStringBuf(content).RSplit(':', host, port);
    return {.Host = ToStdString(host), .Port = FromString<ui16>(port)};
}

}   // namespace NEphemeralNodes

////////////////////////////////////////////////////////////////////////////////

class TClusterNodeTracker
    : public IClusterNodeTracker
    , public std::enable_shared_from_this<TClusterNodeTracker>
{
    using TClusterNodeMap = std::unordered_map<TClusterNodeName, IClusterNodePtr>;
private:
    NInterop::IDirectoryPtr Directory;
    TClusterDirectoryEventHandlerPtr EventHandler{nullptr};

    TClusterNodeMap ClusterNodes;
    TRWMutex RWMutex;

    Settings Settings_;

    Poco::Logger* Logger;

public:
    TClusterNodeTracker(
        NInterop::IDirectoryPtr directory);

    void StartTrack(const Context& context) override;
    void StopTrack() override;

    TClusterNodeTicket EnterCluster(const std::string& host, ui16 port) override;

    TClusterNodeNames ListAvailableNodes() override;
    TClusterNodes GetAvailableNodes() override;

    // Notifications

    void OnUpdate(NInterop::TNodeRevision newRevision);
    void OnRemove();
    void OnError(const TString& errorMessage);

private:
    TClusterDirectoryEventHandlerPtr CreateEventHandler();
    TClusterNodeNames ProcessNodeList(NInterop::TDirectoryListing listing);
    void UpdateClusterNodes(const TClusterNodeNames& nodeNames);
};

////////////////////////////////////////////////////////////////////////////////

TClusterNodeTracker::TClusterNodeTracker(
    NInterop::IDirectoryPtr directory)
    : Directory(std::move(directory))
    , Logger(&Poco::Logger::get("ClusterNodeTracker"))
{
}

void TClusterNodeTracker::StartTrack(const Context& context)
{
    Settings_ = context.getSettingsRef();
    EventHandler = CreateEventHandler();
    Directory->SubscribeToUpdate(/*expectedRevision=*/ -1, EventHandler);
}

void TClusterNodeTracker::StopTrack()
{
    EventHandler->Detach();
}

TClusterNodeTicket TClusterNodeTracker::EnterCluster(const std::string& host, ui16 port)
{
    auto nameHint = NEphemeralNodes::ToNodeNameHint(host, port);
    auto content = NEphemeralNodes::ToNodeContent(host, port);

    return Directory->CreateAndKeepEphemeralNode(
        nameHint,
        content);
}

TClusterNodeNames TClusterNodeTracker::ListAvailableNodes()
{
    auto nodes = GetAvailableNodes();

    TClusterNodeNames listing;
    for (const auto& node : nodes) {
        listing.insert(node->GetName());
    }
    return listing;
}

TClusterNodes TClusterNodeTracker::GetAvailableNodes()
{
    TReadGuard guard(RWMutex);

    TClusterNodes nodes;
    nodes.reserve(ClusterNodes.size());
    for (const auto& entry : ClusterNodes) {
         nodes.push_back(entry.second);
    }
    return nodes;
}

void TClusterNodeTracker::OnUpdate(NInterop::TNodeRevision newRevision)
{
    LOG_DEBUG(Logger, "Cluster directory updated: new revision = " << newRevision);

    NInterop::TDirectoryListing listing;

    try {
        listing = Directory->ListNodes();
    } catch (...) {
        LOG_WARNING(Logger, "Failed to list cluster directory: " << CurrentExceptionText());
        Directory->SubscribeToUpdate(-1, EventHandler);
        return;
    }

    TClusterNodeNames nodeNames;
    try {
        nodeNames = ProcessNodeList(listing);
    } catch (...) {
        LOG_WARNING(Logger, "Failed to process cluster nodes list: " << CurrentExceptionText());
    }

    UpdateClusterNodes(nodeNames);

    Directory->SubscribeToUpdate(listing.Revision, EventHandler);
}

void TClusterNodeTracker::OnRemove()
{
    LOG_WARNING(Logger, "Cluster directory removed");
    Directory->SubscribeToUpdate(-1, EventHandler);
}

void TClusterNodeTracker::OnError(const TString& errorMessage)
{
    LOG_WARNING(Logger, "Error occurred during cluster directory polling: " << ToStdString(errorMessage));
    Directory->SubscribeToUpdate(-1, EventHandler);
}

TClusterDirectoryEventHandlerPtr TClusterNodeTracker::CreateEventHandler()
{
    return std::make_shared<TClusterDirectoryEventHandler>(shared_from_this());
}

TClusterNodeNames TClusterNodeTracker::ProcessNodeList(NInterop::TDirectoryListing listing)
{
    LOG_INFO(
        Logger,
        "Discover " << listing.Children.size() <<
        " node(s) in cluster directory at revision " << listing.Revision);

    TClusterNodeNames nodeNames;

    for (const auto& node : listing.Children) {
        auto name = NEphemeralNodes::ToClusterNodeName(node.Content);

        LOG_DEBUG(
            Logger,
            "Discover cluster node: " << name.ToString() <<
            ", ephemeral node name = " << ToStdString(node.Name));

        nodeNames.insert(name);
    }

    return nodeNames;
}

void TClusterNodeTracker::UpdateClusterNodes(const TClusterNodeNames& newNodeNames)
{
    TWriteGuard guard(RWMutex);

    TClusterNodeMap newClusterNodes;

    for (const auto& nodeName : newNodeNames) {
        auto found = ClusterNodes.find(nodeName);

        if (found != ClusterNodes.end()) {
            newClusterNodes.emplace(nodeName, found->second);
        } else {
            IClusterNodePtr newNode;
            try {
                newNode = CreateClusterNode(nodeName, Settings_);
            } catch (...) {
                LOG_WARNING(Logger, "Failed to create cluster node " << nodeName.ToString());
                // TODO: reschedule
                continue;
            }
            newClusterNodes.emplace(nodeName, newNode);
        }
    }

    ClusterNodes = std::move(newClusterNodes);
}

////////////////////////////////////////////////////////////////////////////////

void TClusterDirectoryEventHandler::OnUpdate(
    const TString& /*path*/,
    NInterop::TNodeRevision newRevision)
{
    if (auto tracker = Tracker.Lock()) {
        tracker->OnUpdate(newRevision);
    }
}

void TClusterDirectoryEventHandler::OnRemove(
    const TString& /*path*/)
{
    if (auto tracker = Tracker.Lock()) {
        tracker->OnRemove();
    }
}

void TClusterDirectoryEventHandler::OnError(
    const TString& /*path*/,
    const TString& errorMessage)
{
    if (auto tracker = Tracker.Lock()) {
        tracker->OnError(errorMessage);
    }
}

void TClusterDirectoryEventHandler::Detach()
{
    Tracker.Release();
}

////////////////////////////////////////////////////////////////////////////////

IClusterNodeTrackerPtr CreateClusterNodeTracker(
    NInterop::ICoordinationServicePtr coordinationService,
    NInterop::IAuthorizationTokenPtr authToken,
    const std::string directoryPath)
{
    auto directory = coordinationService->OpenOrCreateDirectory(
        *authToken,
        ToString(directoryPath));

    return std::make_shared<TClusterNodeTracker>(
        std::move(directory));
}

} // namespace NClickHouse
} // namespace NYT
