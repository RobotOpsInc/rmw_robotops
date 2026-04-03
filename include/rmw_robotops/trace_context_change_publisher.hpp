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

#ifndef RMW_ROBOTOPS__TRACE_CONTEXT_CHANGE_PUBLISHER_HPP_
#define RMW_ROBOTOPS__TRACE_CONTEXT_CHANGE_PUBLISHER_HPP_

#include <cstdint>
#include "rmw/rmw.h"
#include "rmw_robotops/trace_context.hpp"

namespace rmw_robotops
{

/// Maximum length for node name / namespace strings in context change events
/// Matches MAX_NODE_NAME_LENGTH from trace_event_queue.hpp
constexpr size_t MAX_CTX_NODE_NAME_LENGTH = 256;

/// Context change type constants (match robotops_msgs::msg::TraceContextChange)
constexpr uint8_t CONTEXT_CHANGE_ENTERED = 1;  // Callback started with trace context
constexpr uint8_t CONTEXT_CHANGE_EXITED = 2;   // Callback completed, context cleared

/// Fixed-size event struct for trace context changes
/// No heap allocation: safe to enqueue on the robot's hot path
struct TraceContextChangeEvent
{
  uint64_t timestamp_ns;
  char node_name[MAX_CTX_NODE_NAME_LENGTH];
  char node_namespace[MAX_CTX_NODE_NAME_LENGTH];
  uint64_t thread_id;
  char trace_id[TRACE_ID_LENGTH + 1];
  char span_id[SPAN_ID_LENGTH + 1];
  uint8_t change_type;  // 1=CONTEXT_ENTERED, 2=CONTEXT_EXITED

  TraceContextChangeEvent() noexcept
  : timestamp_ns(0), thread_id(0), change_type(0)
  {
    node_name[0] = '\0';
    node_namespace[0] = '\0';
    trace_id[0] = '\0';
    span_id[0] = '\0';
  }
};

/// Enqueue a trace context change event (non-blocking, noexcept)
/// Returns false if the queue is full — caller should record the drop but not fail
bool enqueue_context_change(const TraceContextChangeEvent & event) noexcept;

/// Start the background thread that drains the context change queue and publishes
/// to /robotops/trace_context.  Non-fatal: if this fails the robot continues normally.
rmw_ret_t start_trace_context_change_publisher(rmw_context_t * context) noexcept;

/// Stop the background publisher thread.  Idempotent and safe to call multiple times.
rmw_ret_t stop_trace_context_change_publisher() noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__TRACE_CONTEXT_CHANGE_PUBLISHER_HPP_
