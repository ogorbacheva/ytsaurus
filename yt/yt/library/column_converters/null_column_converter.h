#pragma once

#include "column_converter.h"

namespace NYT::NColumnConverters {

////////////////////////////////////////////////////////////////////////////////

IColumnConverterPtr CreateNullConverter(int columnId);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NColumnConverters
