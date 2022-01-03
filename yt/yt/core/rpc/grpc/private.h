#pragma once

#include "public.h"

////////////////////////////////////////////////////////////////////////////////
// GRPC forward declarations

struct grpc_completion_queue;
struct grpc_server;
struct grpc_byte_buffer;
struct grpc_call;
struct grpc_channel;
struct grpc_channel_credentials;
struct grpc_server_credentials;
struct grpc_auth_context;

////////////////////////////////////////////////////////////////////////////////

namespace NYT::NRpc::NGrpc {

////////////////////////////////////////////////////////////////////////////////

inline const NLogging::TLogger GrpcLogger("Grpc");

////////////////////////////////////////////////////////////////////////////////

class TCompletionQueueTag;

DECLARE_REFCOUNTED_CLASS(TGrpcLibraryLock);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc::NGrpc
