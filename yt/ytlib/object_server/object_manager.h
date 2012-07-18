#pragma once

#include "public.h"
#include "attribute_set.h"
#include "type_handler.h"

#include <ytlib/misc/thread_affinity.h>
#include <ytlib/meta_state/composite_meta_state.h>
#include <ytlib/meta_state/map.h>
#include <ytlib/cell_master/public.h>
#include <ytlib/transaction_server/public.h>
#include <ytlib/object_server/object_manager.pb.h>
#include <ytlib/cypress_server/public.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

//! Provides high-level management and tracking of objects and their attributes.
/*!
 *  \note
 *  Thread affinity: single-threaded
 */
class TObjectManager
    : public NMetaState::TMetaStatePart
{
public:
    //! Initializes a new instance.
    TObjectManager(
        TObjectManagerConfigPtr config,
        NCellMaster::TBootstrap* bootstrap);

    //! Registers a new type handler.
    /*!
     *  It asserts than no handler of this type is already registered.
     */
    void RegisterHandler(IObjectTypeHandlerPtr handler);

    //! Returns the handler for a given type or NULL if the type is unknown.
    IObjectTypeHandlerPtr FindHandler(EObjectType type) const;

    //! Returns the handler for a given type.
    IObjectTypeHandlerPtr GetHandler(EObjectType type) const;
    
    //! Returns the handler for a given id.
    IObjectTypeHandlerPtr GetHandler(const TObjectId& id) const;

    //! Returns the cell id.
    TCellId GetCellId() const;

    //! Creates a new unique object id.
    TObjectId GenerateId(EObjectType type);

    //! Adds a reference.
    void RefObject(const TObjectId& id);
    void RefObject(const TVersionedObjectId& id);
    void RefObject(TObjectWithIdBase* object);
    void RefObject(NCypressServer::ICypressNode* node);

    //! Removes a reference.
    void UnrefObject(const TObjectId& id);
    void UnrefObject(const TVersionedObjectId& id);
    void UnrefObject(TObjectWithIdBase* object);
    void UnrefObject(NCypressServer::ICypressNode* node);

    //! Returns the current reference counter.
    i32 GetObjectRefCounter(const TObjectId& id);

    //! Returns True if an object with the given #id exists.
    bool ObjectExists(const TObjectId& id);

    //! Returns a proxy for the object with the given versioned id or NULL if there's no such object.
    IObjectProxyPtr FindProxy(
        const TObjectId& id,
        NTransactionServer::TTransaction* transaction = NULL);

    //! Returns a proxy for the object with the given versioned id or NULL. Fails if there's no such object.
    IObjectProxyPtr GetProxy(
        const TObjectId& id,
        NTransactionServer::TTransaction* transaction = NULL);

    //! Creates a new empty attribute set.
    TAttributeSet* CreateAttributes(const TVersionedObjectId& id);

    //! Removes an existing attribute set.
    void RemoveAttributes(const TVersionedObjectId& id);

    //! Called when a versioned object is branched.
    void BranchAttributes(
        const TVersionedObjectId& originatingId,
        const TVersionedObjectId& branchedId);

    //! Called when a versioned object is merged during transaction commit.
    void MergeAttributes(
        const TVersionedObjectId& originatingId,
        const TVersionedObjectId& branchedId);

    //! Returns a YPath service that handles all requests.
    /*!
     *  This service supports some special prefix syntax for YPaths:
     */
    NYTree::IYPathServicePtr GetRootService();
    
    //! Executes a YPath verb, logging the change if neccessary.
    /*!
     *  \param id The id of the object that handles the verb.
     *  If the change is logged, this id is written to the changelog and
     *  used afterwards to replay the change.
     *  \param isWrite True if the verb modifies the state and thus must be logged.
     *  \param context The request context.
     *  \param action An action to call that executes the actual verb logic.
     *  
     *  Note that #action takes a context as a parameter. This is because the original #context
     *  gets wrapped to intercept replies and #action gets the wrapped instance.
     */
    void ExecuteVerb(
        const TVersionedObjectId& id,
        bool isWrite,
        NRpc::IServiceContextPtr context,
        TCallback<void(NRpc::IServiceContextPtr)> action);

    DECLARE_METAMAP_ACCESSORS(Attributes, TAttributeSet, TVersionedObjectId);

private:
    typedef TObjectManager TThis;

    class TServiceContextWrapper;
    class TRootService;

    TObjectManagerConfigPtr Config;
    NCellMaster::TBootstrap* Bootstrap;
    std::vector<IObjectTypeHandlerPtr> TypeToHandler;
    TIntrusivePtr<TRootService> RootService;

    // Stores deltas from parent transaction.
    NMetaState::TMetaStateMap<TVersionedObjectId, TAttributeSet> Attributes;

    void SaveKeys(TOutputStream* output);
    void SaveValues(TOutputStream* output);
    void LoadKeys(TInputStream* input);
    void LoadValues(NCellMaster::TLoadContext context, TInputStream* input);
    
    virtual void Clear() OVERRIDE;

    TVoid ReplayVerb(const NProto::TMsgExecuteVerb& message);

    void OnTransactionCommitted(NTransactionServer::TTransaction* transaction);
    void OnTransactionAborted(NTransactionServer::TTransaction* transaction);
    void PromoteCreatedObjects(NTransactionServer::TTransaction* transaction);
    void ReleaseCreatedObjects(NTransactionServer::TTransaction* transaction);

    DECLARE_THREAD_AFFINITY_SLOT(StateThread);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT

