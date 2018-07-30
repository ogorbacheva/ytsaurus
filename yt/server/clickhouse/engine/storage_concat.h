#pragma once

#include "cluster_tracker.h"

#include <yt/server/clickhouse/interop/api.h>

#include <Interpreters/Cluster.h>
#include <Storages/IStorage.h>

namespace NYT {
namespace NClickHouse {

////////////////////////////////////////////////////////////////////////////////

DB::StoragePtr CreateStorageConcat(
    NInterop::IStoragePtr storage,
    NInterop::TTableList tables,
    IExecutionClusterPtr cluster);

} // namespace NClickHouse
} // namespace NYT
