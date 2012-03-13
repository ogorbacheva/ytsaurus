#pragma once

#include "config.h"
#include "holder.h"

#include <ytlib/misc/lease_manager.h>
#include <ytlib/cell_master/public.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunkManager;

//! Controls holder server-side leases.
/*!
 *  Upon receiving a registration request from a holder,
 *  TChunkManager registers its by calling #THolderExpiration::Register.
 *  
 *  It also extends the leases by calling #THolderExpiration::RenewHolderLeases.
 *  
 *  When a lease expires #THolderExpiration triggers holder deregistration
 *  by calling #TChunkManager::InitiateUnregisterHolder.
 *  The latter is a logged operation during which #THolderExpiration::Unregister
 *  gets called.
 *  
 *  Each registered holder carries an additional "Confirmed" flag.
 *  The flag is used to distinguish between holders that were registered during an earlier
 *  epoch (and whose actual liveness is not yet confirmed) and
 *  those holders that have reported a heartbeat during the current epoch.
 *  
 *  This flag is raised automatically in #OnHolderHeartbeat.
 *  
 */
class THolderLeaseTracker
    : public TRefCounted
{
public:
    typedef TIntrusivePtr<THolderLeaseTracker> TPtr;
    typedef TChunkManagerConfig TConfig;

    //! Initializes an instance.
    /*!
     *  \param config A configuration.
     *  \param chunkManager A chunk manager.
     *  \param invoker An invoker used for lease expiration callbacks.
     */
    THolderLeaseTracker(
        TConfig* config,
        NCellMaster::TBootstrap* bootstrap);

    //! Registers the holder and assigns it an initial lease.
    /*!
     *  Initial lease timeout for registered holders is #TChunkManagerConfig::RegisteredHolderTimeout.
     *  For online holders it is decreased to #TChunkManagerConfig::OnlineHolderTimeout.
     */
    void OnHolderRegistered(const THolder& holder, bool confirmed);

    //! Notifies that the holder has become online and hence its lease timeout must be updated.
    void OnHolderOnline(const THolder& holder);

    //! Renews the lease.
    void OnHolderHeartbeat(const THolder& holder);

    //! Unregisters the holder and stop tracking its lease.
    void OnHolderUnregistered(const THolder& holder);

    //! Returns True iff the holder is confirmed.
    bool IsHolderConfirmed(const THolder& holder);


private:
    struct THolderInfo
    {
        THolderInfo(bool confirmed)
            : Confirmed(confirmed)
        { }

        TLeaseManager::TLease Lease;
        bool Confirmed;
    };

    typedef yhash_map<THolderId, THolderInfo> THolderInfoMap;
     
    TConfig::TPtr Config;
    NCellMaster::TBootstrap* Bootstrap;

    THolderInfoMap HolderInfoMap;

    THolderInfo* FindHolderInfo(THolderId holderId);
    THolderInfo& GetHolderInfo(THolderId holderId);
    void RecreateLease(const THolder& holder);

    void OnExpired(THolderId holderId);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
