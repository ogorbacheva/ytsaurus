#pragma once

#include "public.h"
#include "private.h"

#include <yt/server/cell_node/public.h>

#include <yt/server/misc/disk_location.h>

#include <yt/server/job_agent/job.h>

#include <yt/core/misc/public.h>
#include <yt/core/misc/fs.h>

#include <yt/core/logging/log.h>

namespace NYT {
namespace NExecAgent {

////////////////////////////////////////////////////////////////////////////////

class TSlotLocation
    : public TDiskLocation
{
    DEFINE_BYVAL_RO_PROPERTY(int, SessionCount);

public:
    TSlotLocation(
        const TSlotLocationConfigPtr& config,
        const NCellNode::TBootstrap* bootstrap,
        const TString& id,
        const IJobDirectoryManagerPtr& jobDirectoryManager,
        bool enableTmpfs);

    //! Make ./sandbox, ./home/, ./udf and other directories.
    TFuture<void> CreateSandboxDirectories(
        int slotIndex);

    TFuture<void> MakeSandboxCopy(
        int slotIndex,
        ESandboxKind kind,
        const TString& sourcePath,
        const TString& destinationName,
        bool executable);

    TFuture<void> MakeSandboxLink(
        int slotIndex,
        ESandboxKind kind,
        const TString& targetPath,
        const TString& linkName,
        bool executable);

    TFuture<TNullable<TString>> MakeSandboxTmpfs(
        int slotIndex,
        ESandboxKind kind,
        i64 size,
        const TString& path);

    // Set quota, permissions, etc. Must be called when all files are prepared.
    TFuture<void> FinalizeSanboxPreparation(
        int slotIndex,
        TNullable<i64> diskSpaceLimit,
        TNullable<i64> inodeLimit,
        int userId);

    //! Finishes sandbox preparation.
    TFuture<void> MakeConfig(int slotIndex, NYTree::INodePtr config);

    TFuture<void> CleanSandboxes(int slotIndex);

    TString GetSlotPath(int slotIndex) const;

    void IncreaseSessionCount();
    void DecreaseSessionCount();

    NNodeTrackerClient::NProto::TDiskResourcesInfo GetDiskInfo() const;

    void Disable(const TError& error);

private:
    const TSlotLocationConfigPtr Config_;
    const NCellNode::TBootstrap* Bootstrap_;

    const IJobDirectoryManagerPtr JobDirectoryManager_;

    NConcurrency::TActionQueuePtr LocationQueue_;

    const bool EnableTmpfs_;
    const bool HasRootPermissions_;

    std::set<TString> TmpfsPaths_;

    NConcurrency::TReaderWriterSpinLock SlotsLock_;
    THashMap<int, TNullable<i64>> OccupiedSlotToDiskLimit_;

    TDiskHealthCheckerPtr HealthChecker_;

    void ValidateEnabled() const;

    static void ValidateNotExists(const TString& path);

    bool IsInsideTmpfs(const TString& path) const;

    void EnsureNotInUse(const TString& path) const;

    void ForceSubdirectories(const TString& filePath, const TString& sandboxPath) const;

    TString GetSandboxPath(int slotIndex, ESandboxKind sandboxKind) const;
    TString GetConfigPath(int slotIndex) const;
};

DEFINE_REFCOUNTED_TYPE(TSlotLocation)

////////////////////////////////////////////////////////////////////////////////

} // namespace NExecAgent
} // namespace NYT
