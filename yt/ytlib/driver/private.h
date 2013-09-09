#pragma once

#include "public.h"

#include <core/logging/log.h>

namespace NYT {
namespace NDriver {

////////////////////////////////////////////////////////////////////////////////

struct ICommand;
typedef TIntrusivePtr<ICommand> ICommandPtr;

struct ICommandContext;
typedef TIntrusivePtr<ICommandContext> ICommandContextPtr;

////////////////////////////////////////////////////////////////////////////////

extern NLog::TLogger DriverLogger;

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT
