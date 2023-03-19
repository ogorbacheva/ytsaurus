//
//  Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef Y_ABSL_FLAGS_INTERNAL_PROGRAM_NAME_H_
#define Y_ABSL_FLAGS_INTERNAL_PROGRAM_NAME_H_

#include <util/generic/string.h>

#include "y_absl/base/config.h"
#include "y_absl/strings/string_view.h"

// --------------------------------------------------------------------
// Program name

namespace y_absl {
Y_ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// Returns program invocation name or "UNKNOWN" if `SetProgramInvocationName()`
// is never called. At the moment this is always set to argv[0] as part of
// library initialization.
TString ProgramInvocationName();

// Returns base name for program invocation name. For example, if
//   ProgramInvocationName() == "a/b/mybinary"
// then
//   ShortProgramInvocationName() == "mybinary"
TString ShortProgramInvocationName();

// Sets program invocation name to a new value. Should only be called once
// during program initialization, before any threads are spawned.
void SetProgramInvocationName(y_absl::string_view prog_name_str);

}  // namespace flags_internal
Y_ABSL_NAMESPACE_END
}  // namespace y_absl

#endif  // Y_ABSL_FLAGS_INTERNAL_PROGRAM_NAME_H_
