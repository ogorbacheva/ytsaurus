#pragma once

#include <yt/yt/library/codegen/routine_registry.h>

namespace NYT::NQueryClient {

////////////////////////////////////////////////////////////////////////////////

NCodegen::TRoutineRegistry* GetQueryRoutineRegistry();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryClient
