package rpcclient

type Method string

const (
	MethodCreateNode                Method = "CreateNode"
	MethodCreateObject              Method = "CreateObject"
	MethodNodeExists                Method = "ExistsNode"
	MethodRemoveNode                Method = "RemoveNode"
	MethodGetNode                   Method = "GetNode"
	MethodSetNode                   Method = "SetNode"
	MethodMultisetAttributesNode    Method = "MultisetAttributesNode"
	MethodListNode                  Method = "ListNode"
	MethodCopyNode                  Method = "CopyNode"
	MethodMoveNode                  Method = "MoveNode"
	MethodLinkNode                  Method = "LinkNode"
	MethodCheckPermission           Method = "CheckPermission"
	MethodSelectRows                Method = "SelectRows"
	MethodLookupRows                Method = "LookupRows"
	MethodModifyRows                Method = "ModifyRows"
	MethodDeleteRows                Method = "DeleteRows"
	MethodMountTable                Method = "MountTable"
	MethodUnmountTable              Method = "UnmountTable"
	MethodRemountTable              Method = "RemountTable"
	MethodReshardTable              Method = "ReshardTable"
	MethodAlterTable                Method = "AlterTable"
	MethodFreezeTable               Method = "FreezeTable"
	MethodUnfreezeTable             Method = "UnfreezeTable"
	MethodAlterTableReplica         Method = "AlterTableReplica"
	MethodGetInSyncReplicas         Method = "GetInSyncReplicas"
	MethodStartTransaction          Method = "StartTransaction"
	MethodPingTransaction           Method = "PingTransaction"
	MethodAbortTransaction          Method = "AbortTransaction"
	MethodCommitTransaction         Method = "CommitTransaction"
	MethodAddMember                 Method = "AddMember"
	MethodRemoveMember              Method = "RemoveMember"
	MethodAddMaintenance            Method = "AddMaintenance"
	MethodRemoveMaintenance         Method = "RemoveMaintenance"
	MethodTransferAccountResources  Method = "TransferAccountResources"
	MethodTransferPoolResources     Method = "TransferPoolResources"
	MethodStartOperation            Method = "StartOperation"
	MethodAbortOperation            Method = "AbortOperation"
	MethodSuspendOperation          Method = "SuspendOperation"
	MethodResumeOperation           Method = "ResumeOperation"
	MethodCompleteOperation         Method = "CompleteOperation"
	MethodUpdateOperationParameters Method = "UpdateOperationParameters"
	MethodGetOperation              Method = "GetOperation"
	MethodListOperations            Method = "ListOperations"
	MethodListJobs                  Method = "ListJobs"
	MethodGenerateTimestamps        Method = "GenerateTimestamps"
	MethodLockNode                  Method = "LockNode"
	MethodUnlockNode                Method = "UnlockNode"
	MethodDisableChunkLocations     Method = "DisableChunkLocations"
	MethodDestroyChunkLocations     Method = "DestroyChunkLocations"
	MethodResurrectChunkLocations   Method = "ResurrectChunkLocations"
	MethodRequestRestart            Method = "RequestRestart"
)
