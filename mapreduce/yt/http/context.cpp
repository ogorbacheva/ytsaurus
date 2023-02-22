#include "context.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

bool operator==(const TClientContext& lhs, const TClientContext& rhs)
{
    return lhs.ServerName == rhs.ServerName &&
           lhs.Token == rhs.Token &&
           lhs.ServiceTicketAuth == rhs.ServiceTicketAuth &&
           lhs.HttpClient == rhs.HttpClient &&
           lhs.UseTLS == rhs.UseTLS &&
           lhs.TvmOnly == rhs.TvmOnly;
}

bool operator!=(const TClientContext& lhs, const TClientContext& rhs)
{
    return !(rhs == lhs);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
