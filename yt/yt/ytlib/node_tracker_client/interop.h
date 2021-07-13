#pragma once

#include "public.h"

#include <yt/yt/ytlib/node_tracker_client/proto/node_tracker_service.pb.h>

#include <yt/yt/ytlib/data_node_tracker_client/proto/data_node_tracker_service.pb.h>

#include <yt/yt/ytlib/cellar_node_tracker_client/proto/cellar_node_tracker_service.pb.h>

#include <yt/yt/ytlib/tablet_node_tracker_client/proto/tablet_node_tracker_service.pb.h>

#include <yt/yt/ytlib/exec_node_tracker_client/proto/exec_node_tracker_service.pb.h>

#include <yt/yt_proto/yt/client/node_tracker_client/proto/node.pb.h>

namespace NYT::NNodeTrackerClient {

////////////////////////////////////////////////////////////////////////////////

void FillExecNodeStatistics(
    NProto::TNodeStatistics* nodeStatistics,
    const NProto::TExecNodeStatistics& execNodeStatistics);

void FromNodeStatistics(
    NProto::TExecNodeStatistics* execNodeStatistics,
    const NProto::TNodeStatistics& nodeStatistics);

void FromIncrementalHeartbeatRequest(
    NExecNodeTrackerClient::NProto::TReqHeartbeat* execNodeHeartbeat,
    const NProto::TReqIncrementalHeartbeat& incrementalHeartbeat);

void FromFullHeartbeatRequest(
    NExecNodeTrackerClient::NProto::TReqHeartbeat* execNodeHeartbeat,
    const NProto::TReqFullHeartbeat& fullHeartbeat);

void FillExecNodeHeartbeatPart(
    NProto::TReqIncrementalHeartbeat* incrementalHeartbeat,
    const NExecNodeTrackerClient::NProto::TReqHeartbeat& execNodeHeartbeat);

void FillExecNodeHeartbeatPart(
    NProto::TReqFullHeartbeat* fullHeartbeat,
    const NExecNodeTrackerClient::NProto::TReqHeartbeat& execNodeHeartbeat);

void FromIncrementalHeartbeatResponse(
    NExecNodeTrackerClient::NProto::TRspHeartbeat* execNodeHeartbeatResponse,
    const NProto::TRspIncrementalHeartbeat& incrementalHeartbeatResponse);

void FillIncrementalHeartbeatResponse(
    NProto::TRspIncrementalHeartbeat* incrementalHeartbeatResponse,
    const NExecNodeTrackerClient::NProto::TRspHeartbeat& execNodeHeartbeatResponse);

////////////////////////////////////////////////////////////////////////////////

void FillCellarNodeStatistics(
    NProto::TNodeStatistics* nodeStatistics,
    const NProto::TCellarNodeStatistics& cellarNodeStatistics);

void FromNodeStatistics(
    NProto::TCellarNodeStatistics* cellarNodeStatistics,
    const NProto::TNodeStatistics& nodeStatistics);

void FromIncrementalHeartbeatRequest(
    NCellarNodeTrackerClient::NProto::TReqHeartbeat* cellarNodeHeartbeat,
    const NProto::TReqIncrementalHeartbeat& incrementalHeartbeat);

void FromFullHeartbeatRequest(
    NCellarNodeTrackerClient::NProto::TReqHeartbeat* cellarNodeHeartbeat,
    const NProto::TReqFullHeartbeat& fullHeartbeat);

void FillCellarNodeHeartbeatPart(
    NProto::TReqIncrementalHeartbeat* incrementalHeartbeat,
    const NCellarNodeTrackerClient::NProto::TReqHeartbeat& cellarNodeHeartbeat);

void FillCellarNodeHeartbeatPart(
    NProto::TReqFullHeartbeat* fullHeartbeat,
    const NCellarNodeTrackerClient::NProto::TReqHeartbeat& cellarNodeHeartbeat);

void FromIncrementalHeartbeatResponse(
    NCellarNodeTrackerClient::NProto::TRspHeartbeat* cellarNodeHeartbeatResponse,
    const NProto::TRspIncrementalHeartbeat& incrementalHeartbeatResponse);

void FillIncrementalHeartbeatResponse(
    NProto::TRspIncrementalHeartbeat* incrementalHeartbeatResponse,
    const NCellarNodeTrackerClient::NProto::TRspHeartbeat& cellarNodeHeartbeatResponse);

////////////////////////////////////////////////////////////////////////////////

void FromIncrementalHeartbeatRequest(
    NTabletNodeTrackerClient::NProto::TReqHeartbeat* tabletNodeHeartbeat,
    const NProto::TReqIncrementalHeartbeat& incrementalHeartbeat);

void FillTabletNodeHeartbeatPart(
    NProto::TReqIncrementalHeartbeat* incrementalHeartbeat,
    const NTabletNodeTrackerClient::NProto::TReqHeartbeat& tabletNodeHeartbeat);

////////////////////////////////////////////////////////////////////////////////

void FillDataNodeStatistics(
    NProto::TNodeStatistics* nodeStatistics,
    const NProto::TDataNodeStatistics& dataNodeStatistics);

void FromNodeStatistics(
    NProto::TDataNodeStatistics* dataNodeStatistics,
    const NProto::TNodeStatistics& nodeStatistics);

void FromFullHeartbeatRequest(
    NDataNodeTrackerClient::NProto::TReqFullHeartbeat* fullDataNodeHeartbeat,
    const NProto::TReqFullHeartbeat& fullHeartbeat);

void FromIncrementalHeartbeatRequest(
    NDataNodeTrackerClient::NProto::TReqIncrementalHeartbeat* incrementalDataNodeHeartbeat,
    const NProto::TReqIncrementalHeartbeat& incrementalHeartbeat);

void FillDataNodeHeartbeatPart(
    NProto::TReqIncrementalHeartbeat* incrementalHeartbeat,
    const NDataNodeTrackerClient::NProto::TReqIncrementalHeartbeat& incrementalDataNodeHeartbeat);

void FillDataNodeHeartbeatPart(
    NProto::TReqFullHeartbeat* fullHeartbeat,
    const NDataNodeTrackerClient::NProto::TReqFullHeartbeat& fullDataNodeHeartbeat);

void FromFullHeartbeatResponse(
    NDataNodeTrackerClient::NProto::TRspFullHeartbeat* fullDataNodeHeartbeatResponse,
    const NProto::TRspFullHeartbeat& fullHeartbeatResponse);

void FillFullHeartbeatResponse(
    NProto::TRspFullHeartbeat* fullHeartbeatResponse,
    const NDataNodeTrackerClient::NProto::TRspFullHeartbeat& fullDataNodeHeartbeatResponse);

void FromIncrementalHeartbeatResponse(
    NDataNodeTrackerClient::NProto::TRspIncrementalHeartbeat* incrementalDataNodeHeartbeatResponse,
    const NProto::TRspIncrementalHeartbeat& incrementalHeartbeatResponse);

void FillIncrementalHeartbeatResponse(
    NProto::TRspIncrementalHeartbeat* incrementalHeartbeatResponse,
    const NDataNodeTrackerClient::NProto::TRspIncrementalHeartbeat& incrementalDataNodeHeartbeatResponse);

////////////////////////////////////////////////////////////////////////////////

void FillClusterNodeStatistics(
    NProto::TNodeStatistics* nodeStatistics,
    const NProto::TClusterNodeStatistics& clusterNodeStatistics);

void FromNodeStatistics(
    NProto::TClusterNodeStatistics* clusterNodeStatistics,
    const NProto::TNodeStatistics& nodeStatistics);

void FillClusterNodeHeartbeatPart(
    NProto::TReqIncrementalHeartbeat* incrementalHeartbeat,
    const NProto::TReqHeartbeat& clusterNodeHeartbeat);

void FillClusterNodeHeartbeatPart(
    NProto::TReqFullHeartbeat* fullHeartbeat,
    const NProto::TReqHeartbeat& clusterNodeHeartbeat);

void FromIncrementalHeartbeatRequest(
    NProto::TReqHeartbeat* clusterNodeHeartbeat,
    const NProto::TReqIncrementalHeartbeat& incrementalHeartbeat);

void FromFullHeartbeatRequest(
    NProto::TReqHeartbeat* clusterNodeHeartbeat,
    const NProto::TReqFullHeartbeat& fullHeartbeat);

void FromIncrementalHeartbeatResponse(
    NProto::TRspHeartbeat* clusterNodeHeartbeatResponse,
    const NProto::TRspIncrementalHeartbeat& incrementalHeartbeatResponse);

void FillIncrementalHeartbeatResponse(
    NProto::TRspIncrementalHeartbeat* incrementalHeartbeatResponse,
    const NProto::TRspHeartbeat& clusterNodeHeartbeatResponse);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerClient
