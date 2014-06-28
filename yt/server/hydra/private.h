#pragma once

#include "public.h"

#include <core/misc/lazy_ptr.h>

#include <ytlib/hydra/private.h>

namespace NYT {
namespace NHydra {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(TEpochContext)

struct TRemoteSnapshotParams;

DECLARE_REFCOUNTED_CLASS(TSyncFileChangelog)
DECLARE_REFCOUNTED_CLASS(TDecoratedAutomaton)
DECLARE_REFCOUNTED_CLASS(TLeaderRecovery)
DECLARE_REFCOUNTED_CLASS(TFollowerRecovery)
DECLARE_REFCOUNTED_CLASS(TFollowerTracker)
DECLARE_REFCOUNTED_CLASS(TLeaderCommitter)
DECLARE_REFCOUNTED_CLASS(TFollowerCommitter)
DECLARE_REFCOUNTED_CLASS(TChangelogRotation)

////////////////////////////////////////////////////////////////////////////////

//! A special value indicating that the number of records in the previous
//! changelog is undetermined since there is no previous changelog.
const int NonexistingPrevRecordCount = -1;

//! A special value indicating that the number of records in the previous changelog
//! is unknown.
const int UnknownPrevRecordCount = -2;

//! A special value representing an invalid snapshot (or changelog) id.
const int NonexistingSegmentId = -1;

extern const Stroka SnapshotExtension;
extern const Stroka ChangelogExtension;
extern const Stroka ChangelogIndexExtension;
extern const Stroka MultiplexedDirectory;
extern const Stroka SplitSuffix;
extern const Stroka CleanSuffix;

////////////////////////////////////////////////////////////////////////////////

} // namespace NHydra
} // namespace NYT
