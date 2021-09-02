#pragma once

#include <util/generic/hash.h>

#include <tcmalloc/malloc_extension.h>

#include <yt/yt/library/ytprof/profile.pb.h>

namespace NYT::NYTProf {

////////////////////////////////////////////////////////////////////////////////

NProto::Profile ConvertAllocationProfile(const tcmalloc::Profile& snapshot);

NProto::Profile ReadHeapProfile(tcmalloc::ProfileType profileType);

int AbslStackUnwinder(void** frames, int*,
                      int maxFrames, int skipFrames,
                      const void*,
                      int*);

typedef uintptr_t TMemoryTag;

TMemoryTag SetMemoryTag(TMemoryTag newTag);

THashMap<TMemoryTag, ui64> GetEstimatedMemoryUsage();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTProf
