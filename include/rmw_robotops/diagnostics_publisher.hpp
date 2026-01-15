// Copyright 2025 Robot Ops Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RMW_ROBOTOPS__DIAGNOSTICS_PUBLISHER_HPP_
#define RMW_ROBOTOPS__DIAGNOSTICS_PUBLISHER_HPP_

#include "rmw/rmw.h"

namespace rmw_robotops
{

/// Start the background diagnostics publisher thread
/// This creates a dedicated ROS2 node and publisher for /robotops/diagnostics
/// The publisher emits DiagnosticsReport messages at the configured interval
///
/// @param context RMW context (must be initialized)
/// @return RMW_RET_OK on success, error code otherwise
///
/// @note This function is idempotent - calling it multiple times is safe
/// @note This function is noexcept - never throws exceptions
rmw_ret_t start_diagnostics_publisher(rmw_context_t * context) noexcept;

/// Stop the background diagnostics publisher thread
/// This gracefully shuts down the publisher thread and cleans up resources
///
/// @return RMW_RET_OK on success, error code otherwise
///
/// @note This function is idempotent - calling it multiple times is safe
/// @note This function is noexcept - never throws exceptions
rmw_ret_t stop_diagnostics_publisher() noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__DIAGNOSTICS_PUBLISHER_HPP_
