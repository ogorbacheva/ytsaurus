#pragma once

#include "public.h"
#include "common.h"

#include <ytlib/cell_master/public.h>
#include <ytlib/misc/property.h>
#include <ytlib/misc/id_generator.h>
#include <ytlib/misc/lease_manager.h>
#include <ytlib/misc/configurable.h>
#include <ytlib/meta_state/meta_state_manager.h>
#include <ytlib/meta_state/composite_meta_state.h>
#include <ytlib/meta_state/meta_change.h>
#include <ytlib/meta_state/map.h>
#include <ytlib/object_server/object_manager.h>

namespace NYT {
namespace NTransactionServer {

////////////////////////////////////////////////////////////////////////////////

class TTransaction;
class TTransactionManifest;

//! Manages client transactions.
class TTransactionManager
    : public NMetaState::TMetaStatePart
{
    //! Called when a new transaction is started.
    DEFINE_BYREF_RW_PROPERTY(TParamActionList<TTransaction&>, OnTransactionStarted);
    //! Called during transaction commit.
    DEFINE_BYREF_RW_PROPERTY(TParamActionList<TTransaction&>, OnTransactionCommitted);
    //! Called during transaction abort.
    DEFINE_BYREF_RW_PROPERTY(TParamActionList<TTransaction&>, OnTransactionAborted);

public:
    typedef TIntrusivePtr<TTransactionManager> TPtr;

    struct TConfig
        : public TConfigurable
    {
        typedef TIntrusivePtr<TConfig> TPtr;

        TDuration DefaultTransactionTimeout;

        TConfig()
        {
            Register("default_transaction_timeout", DefaultTransactionTimeout)
                .GreaterThan(TDuration())
                .Default(TDuration::Seconds(10));
        }
    };

    //! Creates an instance.
    TTransactionManager(
        TConfig* config,
        NMetaState::IMetaStateManager* metaStateManager,
        NMetaState::TCompositeMetaState* metaState,
        NObjectServer::TObjectManager* objectManager);

    // TODO(babenko): killme
    NObjectServer::TObjectManager* GetObjectManager() const;

    NObjectServer::IObjectProxy::TPtr GetRootTransactionProxy();

    TTransaction& Start(TTransaction* parent, TTransactionManifest* manifest);
    void Commit(TTransaction& transaction);
    void Abort(TTransaction& transaction);
    void RenewLease(const TTransactionId& id);

    DECLARE_METAMAP_ACCESSORS(Transaction, TTransaction, TTransactionId);

    //! Returns the list of all transaction ids on the path up to the root.
    //! This list includes #transactionId itself and #NullTransactionId.
    std::vector<TTransactionId> GetTransactionPath(const TTransactionId& transactionId) const;

private:
    typedef TTransactionManager TThis;
    class TTransactionTypeHandler;
    class TTransactionProxy;
    friend class TTransactionProxy;

    TConfig::TPtr Config;
    NObjectServer::TObjectManager::TPtr ObjectManager;

    NMetaState::TMetaStateMap<TTransactionId, TTransaction> TransactionMap;
    yhash_map<TTransactionId, TLeaseManager::TLease> LeaseMap;

    virtual void OnLeaderRecoveryComplete();
    virtual void OnStopLeading();

    void OnTransactionExpired(const TTransactionId& id);

    void CreateLease(const TTransaction& transaction, TTransactionManifest* manifest);
    void CloseLease(const TTransaction& transaction);
    void FinishTransaction(TTransaction& transaction);

    // TMetaStatePart overrides
    void SaveKeys(TOutputStream* output);
    void SaveValues(TOutputStream* output);
    void LoadKeys(TInputStream* input);
    void LoadValues(TInputStream* input, NCellMaster::TLoadContext context);
    virtual void Clear();

    DECLARE_THREAD_AFFINITY_SLOT(StateThread);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionServer
} // namespace NYT
