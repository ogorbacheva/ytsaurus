// Copyright 2018 The Abseil Authors.
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

#include "y_absl/container/internal/hashtablez_sampler.h"

#include "y_absl/base/attributes.h"

namespace y_absl {
Y_ABSL_NAMESPACE_BEGIN
namespace container_internal {

// See hashtablez_sampler.h for details.
extern "C" Y_ABSL_ATTRIBUTE_WEAK bool Y_ABSL_INTERNAL_C_SYMBOL(
    AbslContainerInternalSampleEverything)() {
  return false;
}

}  // namespace container_internal
Y_ABSL_NAMESPACE_END
}  // namespace y_absl
