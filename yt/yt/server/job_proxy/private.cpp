#include "private.h"

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

const NLogging::TLogger JobProxyLogger("JobProxy");
const TDuration RpcServerShutdownTimeout = TDuration::Seconds(15);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy

