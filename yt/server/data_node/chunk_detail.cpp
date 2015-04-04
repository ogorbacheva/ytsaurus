#include "stdafx.h"
#include "chunk_detail.h"
#include "location.h"
#include "session_manager.h"
#include "private.h"

#include <core/misc/fs.h>

#include <core/concurrency/thread_affinity.h>

#include <server/cell_node/bootstrap.h>

#include <ytlib/chunk_client/chunk_meta_extensions.h>

namespace NYT {
namespace NDataNode {

using namespace NCellNode;
using namespace NChunkClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = DataNodeLogger;

////////////////////////////////////////////////////////////////////////////////

TChunkBase::TChunkBase(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkId& id)
    : Bootstrap_(bootstrap)
    , Location_(location)
    , Id_(id)
{ }

const TChunkId& TChunkBase::GetId() const
{
    return Id_;
}

TLocationPtr TChunkBase::GetLocation() const
{
    return Location_;
}

Stroka TChunkBase::GetFileName() const
{
    return Location_->GetChunkPath(Id_);
}

int TChunkBase::GetVersion() const
{
    return Version_;
}

void TChunkBase::IncrementVersion()
{
    ++Version_;
}

bool TChunkBase::TryAcquireReadLock()
{
    VERIFY_THREAD_AFFINITY_ANY();

    int lockCount;
    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (RemovedPromise_) {
            LOG_DEBUG("Chunk read lock cannot be acquired since removal is already pending (ChunkId: %v)",
                Id_);
            return false;
        }

        lockCount = ++ReadLockCounter_;
    }

    LOG_TRACE("Chunk read lock acquired (ChunkId: %v, LockCount: %v)",
        Id_,
        lockCount);

    return true;
}

void TChunkBase::ReleaseReadLock()
{
    VERIFY_THREAD_AFFINITY_ANY();

    bool removing = false;
    int lockCount;
    {
        TGuard<TSpinLock> guard(SpinLock_);
        lockCount = --ReadLockCounter_;
        YCHECK(lockCount >= 0);
        if (ReadLockCounter_ == 0 && !Removing_ && RemovedPromise_) {
            removing = Removing_ = true;
        }
    }

    LOG_TRACE("Chunk read lock released (ChunkId: %v, LockCount: %v)",
        Id_,
        lockCount);

    if (removing) {
        StartAsyncRemove();
    }
}

bool TChunkBase::IsReadLockAcquired() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    TGuard<TSpinLock> guard(SpinLock_);
    return ReadLockCounter_ > 0;
}

TFuture<void> TChunkBase::ScheduleRemove()
{
    LOG_INFO("Chunk remove scheduled (ChunkId: %v)",
        Id_);

    bool removing = false;
    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (RemovedPromise_) {
            return RemovedPromise_;
        }

        RemovedPromise_ = NewPromise<void>();
        if (ReadLockCounter_ == 0 && !Removing_) {
            removing = Removing_ = true;
        }
    }

    if (removing) {
        StartAsyncRemove();
    }

    return RemovedPromise_;
}

bool TChunkBase::IsRemoveScheduled() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    TGuard<TSpinLock> guard(SpinLock_);
    return static_cast<bool>(RemovedPromise_);
}

void TChunkBase::StartAsyncRemove()
{
    RemovedPromise_.SetFrom(AsyncRemove());
}

TRefCountedChunkMetaPtr TChunkBase::FilterMeta(
    TRefCountedChunkMetaPtr meta,
    const TNullable<std::vector<int>>& extensionTags)
{
    return extensionTags
        ? New<TRefCountedChunkMeta>(FilterChunkMetaByExtensionTags(*meta, extensionTags))
        : meta;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
