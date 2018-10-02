#include "serialize.h"

namespace NYT {
namespace NCellMaster {

////////////////////////////////////////////////////////////////////////////////

int GetCurrentSnapshotVersion()
{
    return 718; // This version is fixed in 19.3. Please do master development in branch 19.4.
}

bool ValidateSnapshotVersion(int version)
{
    return
        version == 354 ||
        version == 355 ||
        version == 356 ||
        version == 400 ||
        version == 401 ||
        version == 402 ||
        version == 403 ||
        version == 500 ||
        version == 501 ||
        version == 502 ||
        version == 503 ||
        version == 504 ||
        version == 505 ||
        version == 506 ||
        version == 507 ||
        version == 508 ||
        version == 600 ||
        version == 601 ||
        version == 602 ||
        version == 603 ||
        version == 604 ||
        version == 605 ||
        version == 606 ||
        version == 607 ||
        version == 608 ||
        version == 609 ||
        version == 610 ||
        version == 611 ||
        version == 612 ||
        version == 613 ||
        version == 614 ||
        version == 615 ||
        version == 616 ||
        version == 617 ||
        version == 618 ||
        version == 619 ||
        version == 620 ||
        version == 621 ||
        version == 622 ||
        version == 623 ||
        version == 624 ||
        version == 625 ||
        version == 626 ||
        version == 627 ||
        version == 628 ||
        version == 629 ||
        version == 630 ||
        version == 700 ||
        version == 701 ||
        version == 702 ||
        version == 703 ||
        version == 704 ||
        version == 705 ||
        version == 706 ||
        version == 707 ||
        version == 708 ||
        version == 709 ||
        version == 710 ||
        version == 711 || // shakurov
        version == 712 || // aozeritsky
        version == 713 || // savrus: Add tablet cell decommission
        version == 714 || // savrus: Change TReqKickOrphanedTabletActions
        version == 715 || // ifsmirnov: Fix tablet_error_count lag
        version == 716 || // savrus: Add dynamic tablet cell options
        version == 717 || // aozeritsky: Add replicated table options
        version == 718;   // shakurov: weak ghosts save/load
}

////////////////////////////////////////////////////////////////////////////////

TLoadContext::TLoadContext(TBootstrap* bootstrap)
    : Bootstrap_(bootstrap)
{ }

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT
