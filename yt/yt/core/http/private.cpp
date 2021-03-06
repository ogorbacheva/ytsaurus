#include "private.h"

namespace NYT::NHttp {

////////////////////////////////////////////////////////////////////////////////

const NLogging::TLogger HttpLogger("Http");
const NProfiling::TProfiler HttpProfiler = NProfiling::TProfiler{"/http"}.WithHot();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHttp
