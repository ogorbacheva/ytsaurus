#pragma once

#include "public.h"

#include <yt/server/lib/controller_agent/persistence.h>

#include <yt/core/logging/log.h>

#include <yt/core/misc/phoenix.h>

namespace NYT::NChunkPools {

////////////////////////////////////////////////////////////////////////////////

using NControllerAgent::TSaveContext;
using NControllerAgent::TLoadContext;
using NControllerAgent::TPersistenceContext;
using NControllerAgent::IPersistent;

// It's quite easy to mix up input cookies with output cookies,
// so we use two following aliases to visually distinguish them.
using TInputCookie = int;
using TOutputCookie = int;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(ILegacySortedJobBuilder);
DECLARE_REFCOUNTED_STRUCT(INewSortedJobBuilder);

DECLARE_REFCOUNTED_CLASS(TNewJobManager)
DECLARE_REFCOUNTED_CLASS(TLegacyJobManager)

DECLARE_REFCOUNTED_CLASS(TOutputOrder)

struct IShuffleChunkPool;

class TInputStreamDirectory;

////////////////////////////////////////////////////////////////////////////////

extern const NLogging::TLogger ChunkPoolLogger;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkPools

