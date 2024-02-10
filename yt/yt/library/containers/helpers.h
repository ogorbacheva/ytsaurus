#pragma once

#include "public.h"

namespace NYT::NContainers {

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TDevice& device, TStringBuf /* format */);

TString ToString(const TDevice& device);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NContainers
