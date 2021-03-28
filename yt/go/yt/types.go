package yt

import (
	"a.yandex-team.ru/yt/go/guid"
	"a.yandex-team.ru/yt/go/ypath"
	"a.yandex-team.ru/yt/go/yson"
)

type NodeType string

const (
	// NodeMap is cypress analog for directory.
	NodeMap NodeType = "map_node"
	// NodeLink is symbolic link.
	NodeLink NodeType = "link"
	// NodeFile is regular file. Used for artifacts and opaque blobs.
	NodeFile NodeType = "file"
	// NodeTable is table.
	NodeTable             NodeType = "table"
	NodeDocument          NodeType = "document"
	NodeTableReplica      NodeType = "table_replica"
	NodeReplicatedTable   NodeType = "replicated_table"
	NodeUser              NodeType = "user"
	NodeGroup             NodeType = "group"
	NodeAccount           NodeType = "account"
	NodeMedium            NodeType = "medium"
	NodeTabletCellBundle  NodeType = "tablet_cell_bundle"
	NodeSys               NodeType = "sys_node"
	NodePortalEntrance    NodeType = "portal_entrance"
	NodePortalExit        NodeType = "portal_exit"
	NodeSchedulerPool     NodeType = "scheduler_pool"
	NodeSchedulerPoolTree NodeType = "scheduler_pool_tree"
)

func (n NodeType) String() string {
	return string(n)
}

type OperationType string

const (
	OperationMap        OperationType = "map"
	OperationReduce     OperationType = "reduce"
	OperationMapReduce  OperationType = "map_reduce"
	OperationSort       OperationType = "sort"
	OperationMerge      OperationType = "merge"
	OperationErase      OperationType = "erase"
	OperationRemoteCopy OperationType = "remote_copy"
	OperationVanilla    OperationType = "vanilla"
)

type OperationState string

const (
	StateRunning       OperationState = "running"
	StatePending       OperationState = "pending"
	StateCompleted     OperationState = "completed"
	StateFailed        OperationState = "failed"
	StateAborted       OperationState = "aborted"
	StateReviving      OperationState = "reviving"
	StateInitializing  OperationState = "initializing"
	StatePreparing     OperationState = "preparing"
	StateMaterializing OperationState = "materializing"
	StateCompleting    OperationState = "completing"
	StateAborting      OperationState = "aborting"
	StateFailing       OperationState = "failing"
)

func (o OperationState) IsFinished() bool {
	switch o {
	case StateCompleted, StateFailed, StateAborted:
		return true
	}

	return false
}

type JobState string

var (
	JobRunning   JobState = "running"
	JobPending   JobState = "pending"
	JobCompleted JobState = "completed"
	JobFailed    JobState = "failed"
	JobAborted   JobState = "aborted"
)

type NodeID guid.GUID

func (id NodeID) String() string {
	return guid.GUID(id).String()
}

func (id NodeID) MarshalYSON(w *yson.Writer) error {
	return guid.GUID(id).MarshalYSON(w)
}

func (id *NodeID) UnmarshalYSON(data []byte) (err error) {
	var g guid.GUID
	err = g.UnmarshalYSON(data)
	*id = NodeID(g)
	return
}

func (id NodeID) MarshalText() ([]byte, error) {
	return guid.GUID(id).MarshalText()
}

func (id *NodeID) UnmarshalText(data []byte) (err error) {
	return (*guid.GUID)(id).UnmarshalText(data)
}

func (id NodeID) YPath() ypath.Path {
	return ypath.Path("#" + id.String())
}

type Revision uint64

type OperationID guid.GUID

func (id OperationID) String() string {
	return guid.GUID(id).String()
}

func (id OperationID) MarshalYSON(w *yson.Writer) error {
	return guid.GUID(id).MarshalYSON(w)
}

func (id *OperationID) UnmarshalYSON(data []byte) (err error) {
	var g guid.GUID
	err = g.UnmarshalYSON(data)
	*id = OperationID(g)
	return
}

type TxID guid.GUID

func (id TxID) String() string {
	return guid.GUID(id).String()
}

func (id TxID) MarshalYSON(w *yson.Writer) error {
	return guid.GUID(id).MarshalYSON(w)
}

func (id *TxID) UnmarshalYSON(data []byte) (err error) {
	var g guid.GUID
	err = g.UnmarshalYSON(data)
	*id = TxID(g)
	return
}

type MutationID guid.GUID

func (id MutationID) String() string {
	return guid.GUID(id).String()
}

func (id MutationID) MarshalYSON(w *yson.Writer) error {
	return guid.GUID(id).MarshalYSON(w)
}

func (id *MutationID) UnmarshalYSON(data []byte) (err error) {
	var g guid.GUID
	err = g.UnmarshalYSON(data)
	*id = MutationID(g)
	return
}

type JobID guid.GUID

func (id JobID) String() string {
	return guid.GUID(id).String()
}

func (id JobID) MarshalYSON(w *yson.Writer) error {
	return guid.GUID(id).MarshalYSON(w)
}

func (id *JobID) UnmarshalYSON(data []byte) (err error) {
	var g guid.GUID
	err = g.UnmarshalYSON(data)
	*id = JobID(g)
	return
}

type LockMode string

const (
	LockSnapshot  LockMode = "snapshot"
	LockShared    LockMode = "shared"
	LockExclusive LockMode = "exclusive"
)

// LockState type holds available lock states.
type LockState string

const (
	// LockPending is a state of a queued waitable lock.
	LockPending LockState = "pending"
	// LockAcquired is a state of an acquired lock.
	LockAcquired LockState = "acquired"
)

type JobSortField string

const (
	NoneSortField       JobSortField = "none"
	TypeSortField       JobSortField = "type"
	StateSortField      JobSortField = "state"
	StartTimeSortField  JobSortField = "start_time"
	FinishTimeSortField JobSortField = "finish_time"
	AddressSortField    JobSortField = "address"
	DurationSortField   JobSortField = "duration"
	ProgressSortField   JobSortField = "progress"
	IDSortField         JobSortField = "id"
)

type JobSortOrder string

const (
	Ascending  JobSortOrder = "ascending"
	Descending JobSortOrder = "descending"
)

type TableReplicaMode string

var (
	SyncMode  TableReplicaMode = "sync"
	AsyncMode TableReplicaMode = "async"
)

type LockType string

const (
	LockTypeNone         LockType = "none"
	LockTypeSharedWeak   LockType = "shared_weak"
	LockTypeSharedStrong LockType = "shared_strong"
	LockTypeExclusive    LockType = "exclusive"
)

// Timestamp is a cluster-wide unique monotonically increasing number used to implement the MVCC.
type Timestamp uint64
