#include "stdafx.h"
#include "resource_tracker.h"
#include "profiler.h"
#include "timing.h"

#include <ytlib/misc/fs.h>
#include <ytlib/misc/proc.h>

#include <ytlib/ypath/token.h>

#include <util/folder/filelist.h>
#include <util/stream/file.h>
#include <util/string/vector.h>

#include <util/private/lfalloc/helpers.h>

////////////////////////////////////////////////////////////////////////////////

namespace NYT {
namespace NProfiling {

using namespace NYPath;

#if !defined(_win_) && !defined(_darwin_)

////////////////////////////////////////////////////////////////////////////////

const TDuration TResourceTracker::UpdateInterval = TDuration::Seconds(1);
static TProfiler Profiler("/resource_tracker");

////////////////////////////////////////////////////////////////////////////////

// Please, refer to /proc documentation to know more about available information.
// http://www.kernel.org/doc/Documentation/filesystems/proc.txt

TResourceTracker::TResourceTracker(IInvokerPtr invoker)
    // CPU time is measured in jiffies; we need USER_HZ to convert them
    // to milliseconds and percentages.
    : TicksPerSecond(sysconf(_SC_CLK_TCK))
    , LastUpdateTime(TInstant::Now())
{
    PeriodicInvoker = New<TPeriodicInvoker>(
        invoker,
        BIND(&TResourceTracker::EnqueueUsage, Unretained(this)),
        UpdateInterval);
}

void TResourceTracker::Start()
{
    PeriodicInvoker->Start();
}

void TResourceTracker::EnqueueUsage()
{
    EnqueueMemoryUsage();
    EnqueueCpuUsage();
}

void TResourceTracker::EnqueueCpuUsage()
{
    ui64 timeDelta = TInstant::Now().MilliSeconds() - LastUpdateTime.MilliSeconds();

    if (timeDelta == 0) {
        return;
    }

    Stroka path = Sprintf("/proc/self/task");
    TDirsList dirsList;
    dirsList.Fill(path);

    for (i32 i = 0; i < dirsList.Size(); ++i) {
        Stroka threadStatPath = NFS::CombinePaths(path, dirsList.Next());
        Stroka cpuStatPath = NFS::CombinePaths(threadStatPath, "stat");

        VectorStrok fields;

        try {
            TIFStream cpuStatFile(cpuStatPath);
            fields = splitStroku(cpuStatFile.ReadLine(), " ");
        } catch (const TIoException&) {
            // Ignore all IO exceptions.
            continue;
        }

        // Get rid of parentheses in process title.
        YCHECK(fields[1].size() >= 2);

        Stroka threadName = fields[1].substr(1, fields[1].size() - 2);
        TYPath pathPrefix = "/" + ToYPathLiteral(threadName);

        i64 userJiffies = FromString<i64>(fields[13]); // In jiffies
        i64 systemJiffies = FromString<i64>(fields[14]); // In jiffies

        auto it = PreviousUserJiffies.find(threadName);
        if (it != PreviousUserJiffies.end()) {
            i64 userCpuTime = (userJiffies - PreviousUserJiffies[threadName]) * 1000 / TicksPerSecond;
            i64 systemCpuTime = (systemJiffies - PreviousSystemJiffies[threadName]) * 1000 / TicksPerSecond;

            Profiler.Enqueue(pathPrefix + "/user_cpu", 100 * userCpuTime / timeDelta);
            Profiler.Enqueue(pathPrefix + "/system_cpu", 100 * systemCpuTime / timeDelta);
        }

        PreviousUserJiffies[threadName] = userJiffies;
        PreviousSystemJiffies[threadName] = systemJiffies;
    }

    LastUpdateTime = TInstant::Now();
}

void TResourceTracker::EnqueueMemoryUsage()
{
    try {
        Profiler.Enqueue("/total/memory", GetProcessRss());
    } catch (const TIoException&) {
        // Ignore all IO exceptions.
        return;
    }
    EnqueueLfAllocCounters();
}

void TResourceTracker::EnqueueLfAllocCounters()
{
    i64 userAllocated = GetLFAllocCounterFull(CT_USER_ALLOC);
    i64 mmaped = GetLFAllocCounterFull(CT_MMAP);
    i64 munmaped = GetLFAllocCounterFull(CT_MUNMAP);
    // Allocated for lf_allow own's needs.
    i64 systemAllocated = GetLFAllocCounterFull(CT_SYSTEM_ALLOC);
    i64 systemDeallocated = GetLFAllocCounterFull(CT_SYSTEM_FREE);
    i64 smallBlocksAllocated = GetLFAllocCounterFull(CT_SMALL_ALLOC);
    i64 smallBlocksDeallocated = GetLFAllocCounterFull(CT_SMALL_FREE);
    i64 largeBlocksAllocated = GetLFAllocCounterFull(CT_LARGE_ALLOC);
    i64 largeBlocksDeallocated = GetLFAllocCounterFull(CT_LARGE_FREE);

    Profiler.Enqueue("/lf_alloc/total/user_allocated", userAllocated);
    Profiler.Enqueue("/lf_alloc/total/mmaped", mmaped);
    Profiler.Enqueue("/lf_alloc/total/munmaped", munmaped);
    Profiler.Enqueue("/lf_alloc/total/system_allocated", systemAllocated);
    Profiler.Enqueue("/lf_alloc/total/system_deallocated", systemDeallocated);
    Profiler.Enqueue("/lf_alloc/total/small_blocks_allocated", smallBlocksAllocated);
    Profiler.Enqueue("/lf_alloc/total/small_blocks_deallocated", smallBlocksDeallocated);
    Profiler.Enqueue("/lf_alloc/total/large_blocks_allocated", largeBlocksAllocated);
    Profiler.Enqueue("/lf_alloc/total/large_blocks_deallocated", largeBlocksDeallocated);

    i64 currentMmaped = mmaped - munmaped;
    Profiler.Enqueue("/lf_alloc/current/mmaped", currentMmaped);
    i64 currentSystem = systemAllocated - systemDeallocated;
    Profiler.Enqueue("/lf_alloc/current/system", currentSystem);
    i64 currentSmallBlocks = smallBlocksAllocated - smallBlocksDeallocated;
    Profiler.Enqueue("/lf_alloc/current/small_blocks", currentSmallBlocks);
    i64 currentLargeBlocks = largeBlocksAllocated - largeBlocksDeallocated;
    Profiler.Enqueue("/lf_alloc/current/large_blocks", currentLargeBlocks);

    i64 currentUsed = currentSystem + currentLargeBlocks + currentSmallBlocks;
    Profiler.Enqueue("/lf_alloc/current/used", currentUsed);
    Profiler.Enqueue("/lf_alloc/current/locked", currentMmaped - currentUsed);
}

////////////////////////////////////////////////////////////////////////////////

#endif

} // namespace NProfiling
} // namespace NYT
