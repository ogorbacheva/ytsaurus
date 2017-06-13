#pragma once

#ifndef CONFIG_INL_H_
#error "Direct inclusion of this file is not allowed, include config.h"
#endif

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

template <class TOptions>
void TSchedulerConfig::UpdateOptions(TOptions* options, NYT::NYTree::INodePtr patch)
{
    using NYTree::INodePtr;
    using NYTree::ConvertTo;

    if (!patch) {
        return;
    }

    if (*options) {
        *options = ConvertTo<TOptions>(UpdateNode(patch, ConvertTo<INodePtr>(*options)));
    } else {
        *options = ConvertTo<TOptions>(patch);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

