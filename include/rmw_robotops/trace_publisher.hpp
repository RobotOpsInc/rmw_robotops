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

#ifndef RMW_ROBOTOPS__TRACE_PUBLISHER_HPP_
#define RMW_ROBOTOPS__TRACE_PUBLISHER_HPP_

#include "rmw/rmw.h"

namespace rmw_robotops
{

/// Start the background trace publisher thread
///
/// Creates a ROS2 publisher on /robotops/trace_events and spawns a background thread
/// that drains the trace event queue and publishes events.
///
/// @param context RMW context (must be initialized)
/// @return RMW_RET_OK on success, error code otherwise
///
/// Safety guarantees:
/// - Non-blocking: Uses try_pop() from queue
/// - Best-effort: Dropped events don't crash the system
/// - Thread-safe: Can be called once per process
rmw_ret_t start_trace_publisher(rmw_context_t * context) noexcept;

/// Stop the background trace publisher thread
///
/// Signals the thread to stop, waits for it to finish, and cleans up resources.
///
/// @return RMW_RET_OK on success, error code otherwise
///
/// Safety guarantees:
/// - Graceful shutdown: Drains remaining events before stopping
/// - Thread-safe: Can be called multiple times (idempotent)
rmw_ret_t stop_trace_publisher() noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__TRACE_PUBLISHER_HPP_
