#include "permission.h"

#include <yt/core/misc/error.h>
#include <yt/core/misc/string.h>

namespace NYT::NYTree {

////////////////////////////////////////////////////////////////////////////////

std::vector<TString> FormatPermissions(EPermissionSet permissions)
{
    std::vector<TString> result;
    for (auto value : TEnumTraits<EPermission>::GetDomainValues()) {
        if (Any(permissions & value)) {
            result.push_back(FormatEnum(value));
        }
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTree

