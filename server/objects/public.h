#pragma once

// This header is the first intentionally.
#include <yp/server/lib/misc/public.h>

#include <yp/server/master/public.h>

#include <yp/server/lib/objects/public.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/misc/guid.h>

#include <variant>

namespace NYP::NServer::NObjects {

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

class TPodSpecEtc;
class TPodStatusEtc;

} // namespace NProto

DECLARE_REFCOUNTED_CLASS(TPodTypeHandlerConfig)
DECLARE_REFCOUNTED_CLASS(TPodSpecValidationConfig)
DECLARE_REFCOUNTED_CLASS(TPodSetTypeHandlerConfig)
DECLARE_REFCOUNTED_CLASS(TNodeSegmentTypeHandlerConfig)
DECLARE_REFCOUNTED_CLASS(TPodVcpuGuaranteeToLimitRatioConstraintConfig)
DECLARE_REFCOUNTED_CLASS(TStageTypeHandlerConfig)

DECLARE_REFCOUNTED_CLASS(TObjectManagerConfig)
DECLARE_REFCOUNTED_CLASS(TObjectManager)

DECLARE_REFCOUNTED_CLASS(TTransactionManagerConfig)
DECLARE_REFCOUNTED_CLASS(TTransactionManager)

DECLARE_REFCOUNTED_CLASS(TWatchManagerConfig)
DECLARE_REFCOUNTED_CLASS(TWatchManager)

DECLARE_REFCOUNTED_CLASS(TTabletReader)
DECLARE_REFCOUNTED_CLASS(TWatchLogReader)
DECLARE_REFCOUNTED_CLASS(TWatchQueryExecutor)

struct IUpdateContext;
DECLARE_REFCOUNTED_CLASS(TTransaction)

struct ISession;
struct IPersistentAttribute;
struct ILoadContext;
struct IStoreContext;
struct IQueryContext;

struct TDBField;
struct TDBTable;

template <class T>
class TScalarAttribute;

class TTimestampAttribute;

template <class TMany, class TOne>
class TManyToOneAttribute;

template <class TOne, class TMany>
class TOneToManyAttribute;

struct IObjectTypeHandler;
class TAccount;
class TDeployTicket;
class TDnsRecordSet;
class TDaemonSet;
class TDynamicResource;
class TEndpoint;
class TEndpointSet;
class TGroup;
class THorizontalPodAutoscaler;
class TInternetAddress;
class TIP4AddressPool;
class TMultiClusterReplicaSet;
class TNetworkProject;
class TNode;
class TNodeSegment;
class TObject;
class TPod;
class TPodDisruptionBudget;
class TPodSet;
class TProject;
class TRelease;
class TReleaseRule;
class TReplicaSet;
class TResource;
class TResourceCache;
class TSchema;
class TStage;
class TSubject;
class TUser;
class TVirtualService;

class TAttributeSchema;

template <class TTypedObject, class TTypedValue>
struct TScalarAttributeSchema;

template <class T>
class TScalarAttribute;

class TTimestampAttribute;

template <class TMany, class TOne>
struct TManyToOneAttributeSchema;

template <class TMany, class TOne>
class TManyToOneAttribute;

struct TOneToManyAttributeSchemaBase;

template <class TOne, class TMany>
struct TOneToManyAttributeSchema;

template <class TOne, class TMany>
class TOneToManyAttribute;

class TChildrenAttributeBase;

class TAnnotationsAttribute;

DEFINE_ENUM(EObjectState,
    (Unknown)
    (Instantiated)
    (Creating)
    (Created)
    (Removing)
    (Removed)
    (CreatedRemoving)
    (CreatedRemoved)
);

DEFINE_STRING_SERIALIZABLE_ENUM(EPodCurrentState,
    ((Unknown)         (0))
    ((StartPending)  (100))
    ((Started)       (200))
    ((StopPending)   (300))
    ((Stopped)       (400))
    ((StartFailed)   (500))
);

DEFINE_STRING_SERIALIZABLE_ENUM(EPodTargetState,
    ((Removed)         (0))
    ((Active)        (100))
);

DEFINE_ENUM(EEvictionState,
    ((None)           (  0))
    ((Requested)      (100))
    ((Acknowledged)   (200))
);

DEFINE_ENUM(ESchedulingState,
    ((None)           (  0))
    ((Disabled)       (100))
    ((Pending)        (200))
    ((Assigned)       (300))
);

DEFINE_ENUM(EEventType,
    ((None)           (0))
    ((ObjectCreated)  (1))
    ((ObjectRemoved)  (2))
    ((ObjectUpdated)  (3))
);

DEFINE_ENUM(EDeployPatchActionType,
    ((None)   (0))
    ((Commit) (1))
    ((Skip)   (2))
    ((OnHold) (3))
    ((Wait)   (4))
);

DEFINE_ENUM(EDeployTicketPatchSelectorType,
    ((None)    (0))
    ((Full)    (1))
    ((Partial) (2))
);

constexpr int TypicalColumnCountPerDBTable = 16;

using NClient::NApi::TObjectId;
using NClient::NApi::TTransactionId;

using NMaster::TClusterTag;
using NMaster::TMasterInstanceTag;

// Built-in users.
extern const TObjectId RootUserId;

// Built-in groups.
extern const TObjectId SuperusersGroupId;

// Built-in accounts.
extern const TObjectId TmpAccountId;

// Built-in node segments.
extern const TObjectId DefaultNodeSegmentId;

// Pseudo-subjects.
extern const TObjectId EveryoneSubjectId;

// Built-in pool of ip4 addresses .
extern const TObjectId DefaultIP4AddressPoolId;

////////////////////////////////////////////////////////////////////////////////

struct TGenericClearUpdate
{ };

struct TGenericPreserveUpdate
{ };

template <class TValue>
using TGenericUpdate = std::variant<
    TGenericClearUpdate,
    TGenericPreserveUpdate,
    TValue>;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NObjects
