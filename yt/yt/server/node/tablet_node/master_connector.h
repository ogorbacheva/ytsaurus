#pragma once

#include "public.h"

#include <yt/server/lib/tablet_node/public.h>

#include <yt/server/node/cluster_node/public.h>

#include <yt/ytlib/tablet_node_tracker_client/proto/tablet_node_tracker_service.pb.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

//! Mediates connection between a tablet node and its master.
/*!
 *  \note
 *  Thread affinity: Control
 */
struct IMasterConnector
    : public TRefCounted
{
    //! Initialize master connector.
    virtual void Initialize() = 0;

    //! Schedules next tablet node heartbeat.
    /*!
    *  \note
    *  Thread affinity: any
    */
    virtual void ScheduleHeartbeat(NObjectClient::TCellTag cellTag, bool immediately) = 0;

    //! Return tablet node master heartbeat request for a given cell. This function is used only for compatibility
    //! with legacy master connector and will be removed after switching to new heartbeats.
    virtual NTabletNodeTrackerClient::NProto::TReqHeartbeat GetHeartbeatRequest(NObjectClient::TCellTag cellTag) const = 0;

    //! Process tablet node master heartbeat response. This function is used only for compatibility
    //! with legacy master connector and will be removed after switching to new heartbeats.
    virtual void OnHeartbeatResponse(const NTabletNodeTrackerClient::NProto::TRspHeartbeat& response) = 0;
};

DEFINE_REFCOUNTED_TYPE(IMasterConnector)

////////////////////////////////////////////////////////////////////////////////

IMasterConnectorPtr CreateMasterConnector(NClusterNode::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
