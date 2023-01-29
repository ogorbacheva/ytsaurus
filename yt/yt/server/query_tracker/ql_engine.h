#pragma once

#include "private.h"

#include "engine.h"

namespace NYT::NQueryTracker {

///////////////////////////////////////////////////////////////////////////////

IQueryEnginePtr CreateQlEngine(const NApi::IClientPtr& stateClient, const NYPath::TYPath& stateRoot);

///////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryTracker
