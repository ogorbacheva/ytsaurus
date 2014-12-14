#pragma once

#include "public.h"

#include <ytlib/api/public.h>

#include <ytlib/ypath/public.h>

#include <ytlib/transaction_client/public.h>

namespace NYT {
namespace NHydra {

////////////////////////////////////////////////////////////////////////////////

IChangelogStorePtr CreateRemoteChangelogStore(
    TRemoteChangelogStoreConfigPtr config,
    TRemoteChangelogStoreOptionsPtr options,
    const NYPath::TYPath& remotePath,
    NApi::IClientPtr masterClient,
    const std::vector<NTransactionClient::TTransactionId>& prerequisiteTransactionIds =
        std::vector<NTransactionClient::TTransactionId>());

////////////////////////////////////////////////////////////////////////////////

} // namespace NHydra
} // namespace NYT
