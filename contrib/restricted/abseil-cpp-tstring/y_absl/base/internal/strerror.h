// Copyright 2020 The Abseil Authors.
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

#ifndef Y_ABSL_BASE_INTERNAL_STRERROR_H_
#define Y_ABSL_BASE_INTERNAL_STRERROR_H_

#include <util/generic/string.h>

#include "y_absl/base/config.h"

namespace y_absl {
Y_ABSL_NAMESPACE_BEGIN
namespace base_internal {

// A portable and thread-safe alternative to C89's `strerror`.
//
// The C89 specification of `strerror` is not suitable for use in a
// multi-threaded application as the returned string may be changed by calls to
// `strerror` from another thread.  The many non-stdlib alternatives differ
// enough in their names, availability, and semantics to justify this wrapper
// around them.  `errno` will not be modified by a call to `y_absl::StrError`.
TString StrError(int errnum);

}  // namespace base_internal
Y_ABSL_NAMESPACE_END
}  // namespace y_absl

#endif  // Y_ABSL_BASE_INTERNAL_STRERROR_H_
