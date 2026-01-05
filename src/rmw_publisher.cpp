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

#include "rmw/rmw.h"
#include "rmw/error_handling.h"

#include "rmw_robotops/config.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/dds_metadata.hpp"

#include <chrono>
#include <cstring>

// Forward declaration of underlying RMW functions
// These will be dynamically loaded from the underlying RMW implementation
extern "C" {
extern rmw_ret_t (* underlying_rmw_publish)(
  const rmw_publisher_t *, const void *, rmw_publisher_allocation_t *);
extern rmw_publisher_t * (* underlying_rmw_create_publisher)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_publisher_options_t *);
extern rmw_ret_t (* underlying_rmw_destroy_publisher)(
  rmw_node_t *, rmw_publisher_t *);
}

extern "C"
{

rmw_publisher_t *
rmw_create_publisher(
  const rmw_node_t * node,
  const rosidl_message_type_support_t * type_support,
  const char * topic_name,
  const rmw_qos_profile_t * qos_profile,
  const rmw_publisher_options_t * publisher_options)
{
  // Delegate to underlying RMW - no interception needed for creation
  if (underlying_rmw_create_publisher == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return nullptr;
  }

  return underlying_rmw_create_publisher(
    node, type_support, topic_name, qos_profile, publisher_options);
}

rmw_ret_t
rmw_destroy_publisher(rmw_node_t * node, rmw_publisher_t * publisher)
{
  // Delegate to underlying RMW - no interception needed for destruction
  if (underlying_rmw_destroy_publisher == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  return underlying_rmw_destroy_publisher(node, publisher);
}

rmw_ret_t
rmw_publish(
  const rmw_publisher_t * publisher,
  const void * ros_message,
  rmw_publisher_allocation_t * allocation)
{
  using namespace rmw_robotops;

  // Safety guarantee: Real message delivery NEVER blocked by tracing
  // Call underlying RMW first, emit trace event after

  if (underlying_rmw_publish == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // STEP 1: Publish the real message (ALWAYS happens, even if tracing disabled)
  rmw_ret_t ret = underlying_rmw_publish(publisher, ros_message, allocation);

  // STEP 2: Emit trace event (best-effort, never fails the publish)
  if (ret == RMW_RET_OK && is_tracing_enabled()) {
    try {
      // Get or create trace context for this thread
      TraceContext context = get_or_mint_trace_context();

      // Create trace event
      TraceEvent event;
      event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

      // Copy trace context
      std::strncpy(event.trace_id, context.trace_id, TRACE_ID_LENGTH);
      event.trace_id[TRACE_ID_LENGTH] = '\0';
      std::strncpy(event.span_id, context.span_id, SPAN_ID_LENGTH);
      event.span_id[SPAN_ID_LENGTH] = '\0';
      std::strncpy(event.parent_span_id, context.parent_span_id, SPAN_ID_LENGTH);
      event.parent_span_id[SPAN_ID_LENGTH] = '\0';

      // Event details
      event.operation = OP_PUBLISH;
      std::strncpy(
        event.topic_or_service,
        publisher->topic_name,
        MAX_TOPIC_NAME_LENGTH - 1);
      event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

      // TODO(ROB-55): Fill in additional metadata
      // - node_name, node_namespace (from publisher->implementation_identifier)
      // - publisher_gid (from DDS)
      // - sequence_number (from DDS)
      // - message_type (from type_support)
      // - message_size_bytes (measure serialized size)

      // Try to inject context into DDS metadata (currently no-op placeholder)
      inject_trace_context_to_dds(nullptr, context);

      // Try to push to trace event queue (non-blocking)
      if (!get_trace_event_queue().try_push(event)) {
        // Queue full - drop event (safety guarantee: never block)
        // This is logged but doesn't fail the publish
        record_trace_failure();
      } else {
        record_trace_success();
      }
    } catch (...) {
      // Safety guarantee: Never propagate exceptions from tracing code
      // Catch all exceptions and continue
      record_trace_failure();
    }
  }

  return ret;
}

rmw_ret_t
rmw_publish_serialized_message(
  const rmw_publisher_t * publisher,
  const rmw_serialized_message_t * serialized_message,
  rmw_publisher_allocation_t * allocation)
{
  // TODO(ROB-55): Implement serialized message publish with tracing
  // For now, this is unimplemented and will cause a link error if called
  (void)publisher;
  (void)serialized_message;
  (void)allocation;
  RMW_SET_ERROR_MSG("rmw_publish_serialized_message not yet implemented");
  return RMW_RET_UNSUPPORTED;
}

}  // extern "C"
