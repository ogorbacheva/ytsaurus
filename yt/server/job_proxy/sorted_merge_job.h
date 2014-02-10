﻿#pragma once

#include "public.h"
#include "job.h"

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

TJobPtr CreateSortedMergeJob(IJobHost* host);

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
