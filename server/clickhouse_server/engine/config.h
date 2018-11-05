#pragma once

#include <Poco/Util/LayeredConfiguration.h>
#include <Poco/AutoPtr.h>

#include <string>

namespace NYT {
namespace NClickHouseServer {
namespace NEngine {

////////////////////////////////////////////////////////////////////////////////

using IConfigPtr = Poco::AutoPtr<Poco::Util::AbstractConfiguration>;

////////////////////////////////////////////////////////////////////////////////

IConfigPtr LoadConfigFromLocalFile(const std::string& path);

////////////////////////////////////////////////////////////////////////////////

} // namespace NEngine
} // namespace NClickHouseServer
} // namespace NYT
