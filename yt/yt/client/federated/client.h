#pragma once

#include "public.h"

#include <yt/yt/client/api/public.h>

#include <vector>

//! Federated client is a wrapper for several YT-clients with ability
//! to retry asynchronously the same request with different underlying-clients.
//! Each YT-client typically corresponds to a different YT-cluster.
//! In case of errors (for example, cluster unavailability) federated client tries
//! to retry request via another client (cluster).
//!
//! Client in the same datacenter is more prior than other.
//!
//! Federated client implements IClient interface, but doesn's support
//! the most of mutable methods (except modifications inside transactions).
namespace NYT::NClient::NFederated {

////////////////////////////////////////////////////////////////////////////////

// @brief Method for creating federated client with given underlying clients.
NApi::IClientPtr CreateFederatedClient(const std::vector<NApi::IClientPtr>& clients, TFederatedClientConfigPtr config);

////////////////////////////////////////////////////////////////////////////////

} // NYT::NClient::NFederated
