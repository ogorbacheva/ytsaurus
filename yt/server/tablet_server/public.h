#pragma once

#include <yt/ytlib/hydra/public.h>

#include <yt/ytlib/tablet_client/public.h>

#include <yt/core/misc/enum.h>
#include <yt/core/misc/public.h>

namespace NYT {
namespace NTabletServer {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ETabletCellHealth,
    ((Initializing)(0))
    ((Good)        (1))
    ((Degraded)    (2))
    ((Failed)      (3))
);

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TTabletManager)

DECLARE_REFCOUNTED_CLASS(TTabletManagerConfig)

class TTabletCell;
class TTablet;

struct TTabletStatistics;
struct TTabletPerformanceCounter;
struct TTabletPerformanceCounters;

////////////////////////////////////////////////////////////////////////////////

using NHydra::TPeerId;
using NHydra::InvalidPeerId;
using NHydra::EPeerState;

using NTabletClient::TTabletCellId;
using NTabletClient::NullTabletCellId;
using NTabletClient::TTabletId;
using NTabletClient::NullTabletId;
using NTabletClient::TStoreId;
using NTabletClient::ETabletState;
using NTabletClient::TypicalCellSize;

using NTabletClient::TTabletCellConfig;
using NTabletClient::TTabletCellConfigPtr;
using NTabletClient::TTabletCellOptions;
using NTabletClient::TTabletCellOptionsPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletServer
} // namespace NYT
