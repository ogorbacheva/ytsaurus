#pragma once

#include <yt/core/logging/log.h>

namespace NYP {
namespace NServer {
namespace NObjects {

////////////////////////////////////////////////////////////////////////////////

extern const NYT::NLogging::TLogger Logger;

static constexpr int DbVersion = 2;

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjects
} // namespace NServer
} // namespace NYP
