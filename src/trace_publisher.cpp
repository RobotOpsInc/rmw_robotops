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

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw_robotops/config.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "rmw_robotops/trace_publisher.hpp"
#include "robotops_msgs/msg/trace_event.h"
#include "rosidl_runtime_c/primitives_sequence_functions.h"
#include "rosidl_runtime_c/string_functions.h"

// Forward declarations of underlying RMW functions
extern "C" {
extern rmw_node_t * (* underlying_rmw_create_node)(
  rmw_context_t *, const char *, const char *);
extern rmw_ret_t (* underlying_rmw_destroy_node)(rmw_node_t *);
extern rmw_publisher_t * (* underlying_rmw_create_publisher)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_publisher_options_t *);
extern rmw_ret_t (* underlying_rmw_destroy_publisher)(
  rmw_node_t *, rmw_publisher_t *);
extern rmw_ret_t (* underlying_rmw_publish)(
  const rmw_publisher_t *, const void *, rmw_publisher_allocation_t *);
}

namespace rmw_robotops
{

namespace
{

// Background publisher state
std::atomic<bool> publisher_running{false};
std::thread publisher_thread;
rmw_node_t * trace_node = nullptr;
rmw_publisher_t * trace_publisher = nullptr;

/// Convert internal TraceEvent to ROS2 message
/// @note This allocates memory for the ROS2 message - that's OK because
///       this runs in a background thread, not the robot's hot path
bool convert_to_ros_message(
  const TraceEvent & event,
  robotops_msgs__msg__TraceEvent & msg) noexcept
{
  try {
    // Timestamp (convert nanoseconds to sec + nanosec)
    msg.timestamp.sec = static_cast<int32_t>(event.timestamp_ns / 1000000000ULL);
    msg.timestamp.nanosec = static_cast<uint32_t>(event.timestamp_ns % 1000000000ULL);

    // Trace identification
    rosidl_runtime_c__String__assign(&msg.trace_id, event.trace_id);
    rosidl_runtime_c__String__assign(&msg.span_id, event.span_id);
    rosidl_runtime_c__String__assign(&msg.parent_span_id, event.parent_span_id);

    // Span links (convert char[][] to string sequence)
    if (!rosidl_runtime_c__String__Sequence__init(&msg.span_links, event.span_link_count)) {
      return false;
    }
    for (size_t i = 0; i < event.span_link_count; ++i) {
      rosidl_runtime_c__String__assign(&msg.span_links.data[i], event.span_links[i]);
    }

    // Event type for hierarchical span reconstruction
    msg.event_type = event.event_type;

    // Topic/service identification
    rosidl_runtime_c__String__assign(&msg.topic_or_service, event.topic_or_service);
    rosidl_runtime_c__String__assign(&msg.node_name, event.node_name);
    rosidl_runtime_c__String__assign(&msg.node_namespace, event.node_namespace);

    // Correlation metadata
    rosidl_runtime_c__String__assign(&msg.publisher_gid, event.publisher_gid);
    msg.sequence_number = event.sequence_number;
    msg.source_timestamp_ns = event.source_timestamp_ns;
    msg.content_hash = event.content_hash;

    // Pointer for span reconstruction (correlation with ros2_tracing)
    msg.msg_ptr = event.msg_ptr;

    // Message metadata
    rosidl_runtime_c__String__assign(&msg.message_type, event.message_type);
    msg.message_size_bytes = event.message_size_bytes;

    // DDS metadata
    msg.dds_domain_id = event.dds_domain_id;
    msg.correlation_method = event.correlation_method;

    return true;
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
    return false;
  }
}

/// Background thread function that publishes trace events
void publisher_thread_func() noexcept
{
  robotops_msgs__msg__TraceEvent msg;
  robotops_msgs__msg__TraceEvent__init(&msg);

  while (publisher_running.load(std::memory_order_relaxed)) {
    // Try to pop an event from the queue (non-blocking)
    TraceEvent event;
    if (get_trace_event_queue().try_pop(event)) {
      // Convert to ROS2 message
      if (convert_to_ros_message(event, msg)) {
        // Publish using underlying RMW (bypasses our own interception)
        if (trace_publisher != nullptr && underlying_rmw_publish != nullptr) {
          underlying_rmw_publish(trace_publisher, &msg, nullptr);
        }

        // Clean up message strings for next iteration
        robotops_msgs__msg__TraceEvent__fini(&msg);
        robotops_msgs__msg__TraceEvent__init(&msg);
      }
    } else {
      // Queue empty - sleep briefly to avoid spinning
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Drain remaining events before shutdown
  TraceEvent event;
  while (get_trace_event_queue().try_pop(event)) {
    if (convert_to_ros_message(event, msg)) {
      if (trace_publisher != nullptr && underlying_rmw_publish != nullptr) {
        underlying_rmw_publish(trace_publisher, &msg, nullptr);
      }
      robotops_msgs__msg__TraceEvent__fini(&msg);
      robotops_msgs__msg__TraceEvent__init(&msg);
    }
  }

  robotops_msgs__msg__TraceEvent__fini(&msg);
}

}  // anonymous namespace

rmw_ret_t start_trace_publisher(rmw_context_t * context) noexcept
{
  // Check if already running
  if (publisher_running.load(std::memory_order_relaxed)) {
    return RMW_RET_OK;  // Idempotent
  }

  // Check if tracing is enabled
  if (!is_tracing_enabled()) {
    return RMW_RET_OK;  // No-op if tracing disabled
  }

  // Create a node for the trace publisher
  if (underlying_rmw_create_node == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  trace_node = underlying_rmw_create_node(context, "rmw_robotops_tracer", "/robotops");
  if (trace_node == nullptr) {
    RMW_SET_ERROR_MSG("Failed to create trace publisher node");
    return RMW_RET_ERROR;
  }

  // Get type support for TraceEvent message
  const rosidl_message_type_support_t * type_support =
    ROSIDL_GET_MSG_TYPE_SUPPORT(robotops_msgs, msg, TraceEvent);
  if (type_support == nullptr) {
    underlying_rmw_destroy_node(trace_node);
    trace_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to get TraceEvent type support");
    return RMW_RET_ERROR;
  }

  // Create publisher with best-effort QoS
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 100;

  rmw_publisher_options_t pub_options = rmw_get_default_publisher_options();

  if (underlying_rmw_create_publisher == nullptr) {
    underlying_rmw_destroy_node(trace_node);
    trace_node = nullptr;
    RMW_SET_ERROR_MSG("Underlying RMW create_publisher not available");
    return RMW_RET_ERROR;
  }

  trace_publisher = underlying_rmw_create_publisher(
    trace_node, type_support, "/robotops/trace_events", &qos, &pub_options);

  if (trace_publisher == nullptr) {
    underlying_rmw_destroy_node(trace_node);
    trace_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to create trace publisher");
    return RMW_RET_ERROR;
  }

  // Start background thread
  publisher_running.store(true, std::memory_order_relaxed);
  try {
    publisher_thread = std::thread(publisher_thread_func);
  } catch (...) {
    publisher_running.store(false, std::memory_order_relaxed);
    underlying_rmw_destroy_publisher(trace_node, trace_publisher);
    underlying_rmw_destroy_node(trace_node);
    trace_publisher = nullptr;
    trace_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to start publisher thread");
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

rmw_ret_t stop_trace_publisher() noexcept
{
  // Check if running
  if (!publisher_running.load(std::memory_order_relaxed)) {
    return RMW_RET_OK;  // Idempotent
  }

  // Signal thread to stop
  publisher_running.store(false, std::memory_order_relaxed);

  // Wait for thread to finish
  if (publisher_thread.joinable()) {
    try {
      publisher_thread.join();
    } catch (...) {
      // Safety guarantee: Never propagate exceptions
    }
  }

  // Clean up publisher and node
  if (trace_publisher != nullptr && underlying_rmw_destroy_publisher != nullptr) {
    underlying_rmw_destroy_publisher(trace_node, trace_publisher);
    trace_publisher = nullptr;
  }

  if (trace_node != nullptr && underlying_rmw_destroy_node != nullptr) {
    underlying_rmw_destroy_node(trace_node);
    trace_node = nullptr;
  }

  return RMW_RET_OK;
}

}  // namespace rmw_robotops
