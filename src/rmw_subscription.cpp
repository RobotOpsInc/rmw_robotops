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
extern "C" {
extern rmw_subscription_t * (* underlying_rmw_create_subscription)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_subscription_options_t *);
extern rmw_ret_t (* underlying_rmw_destroy_subscription)(
  rmw_node_t *, rmw_subscription_t *);
extern rmw_ret_t (* underlying_rmw_take)(
  const rmw_subscription_t *, void *, bool *, rmw_subscription_allocation_t *);
extern rmw_ret_t (* underlying_rmw_take_with_info)(
  const rmw_subscription_t *, void *, bool *,
  rmw_message_info_t *, rmw_subscription_allocation_t *);
}

extern "C"
{

rmw_subscription_t *
rmw_create_subscription(
  const rmw_node_t * node,
  const rosidl_message_type_support_t * type_support,
  const char * topic_name,
  const rmw_qos_profile_t * qos_profile,
  const rmw_subscription_options_t * subscription_options)
{
  // Delegate to underlying RMW - no interception needed for creation
  if (underlying_rmw_create_subscription == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return nullptr;
  }

  return underlying_rmw_create_subscription(
    node, type_support, topic_name, qos_profile, subscription_options);
}

rmw_ret_t
rmw_destroy_subscription(rmw_node_t * node, rmw_subscription_t * subscription)
{
  // Delegate to underlying RMW - no interception needed for destruction
  if (underlying_rmw_destroy_subscription == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  return underlying_rmw_destroy_subscription(node, subscription);
}

rmw_ret_t
rmw_take(
  const rmw_subscription_t * subscription,
  void * ros_message,
  bool * taken,
  rmw_subscription_allocation_t * allocation)
{
  using namespace rmw_robotops;

  // Safety guarantee: Real message delivery NEVER blocked by tracing
  // Take the message first, then handle tracing

  if (underlying_rmw_take == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // STEP 1: Take the real message (ALWAYS happens, even if tracing disabled)
  rmw_ret_t ret = underlying_rmw_take(subscription, ros_message, taken, allocation);

  // STEP 2: Handle trace context (best-effort, never fails the take)
  if (ret == RMW_RET_OK && *taken && is_tracing_enabled()) {
    try {
      // Try to extract trace context from DDS metadata
      TraceContext extracted_context;
      bool context_found = extract_trace_context_from_dds(nullptr, extracted_context);

      if (context_found && !extracted_context.is_empty()) {
        // Create new span with extracted context as parent
        TraceContext new_context;
        std::strncpy(new_context.trace_id, extracted_context.trace_id, TRACE_ID_LENGTH);
        new_context.trace_id[TRACE_ID_LENGTH] = '\0';

        // Generate new span_id for this subscription event
        generate_span_id(new_context.span_id);

        // Set parent to the publisher's span
        std::strncpy(
          new_context.parent_span_id,
          extracted_context.span_id,
          SPAN_ID_LENGTH);
        new_context.parent_span_id[SPAN_ID_LENGTH] = '\0';

        // Set as current context for this thread
        set_trace_context(new_context);
      } else {
        // No context found - this could be:
        // 1. First message in a trace (mint new root context)
        // 2. Message from non-traced node
        // 3. Cross-process message (not yet supported)

        // For now, mint a new trace context
        TraceContext new_context = TraceContext::empty();
        generate_trace_id(new_context.trace_id);
        generate_span_id(new_context.span_id);
        new_context.parent_span_id[0] = '\0';  // Root span
        set_trace_context(new_context);
      }

      // Emit subscribe trace event
      TraceEvent event;
      event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

      TraceContext current = get_trace_context();
      std::strncpy(event.trace_id, current.trace_id, TRACE_ID_LENGTH);
      event.trace_id[TRACE_ID_LENGTH] = '\0';
      std::strncpy(event.span_id, current.span_id, SPAN_ID_LENGTH);
      event.span_id[SPAN_ID_LENGTH] = '\0';
      std::strncpy(event.parent_span_id, current.parent_span_id, SPAN_ID_LENGTH);
      event.parent_span_id[SPAN_ID_LENGTH] = '\0';

      event.operation = OP_SUBSCRIBE;
      std::strncpy(
        event.topic_or_service,
        subscription->topic_name,
        MAX_TOPIC_NAME_LENGTH - 1);
      event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

      // TODO(ROB-55): Fill in additional metadata
      // - node_name, node_namespace
      // - publisher_gid (from message_info)
      // - sequence_number (from message_info)
      // - message_type, message_size_bytes

      // Try to push to trace event queue (non-blocking)
      if (!get_trace_event_queue().try_push(event)) {
        record_trace_failure();
      } else {
        record_trace_success();
      }
    } catch (...) {
      // Safety guarantee: Never propagate exceptions
      record_trace_failure();
    }
  }

  return ret;
}

rmw_ret_t
rmw_take_with_info(
  const rmw_subscription_t * subscription,
  void * ros_message,
  bool * taken,
  rmw_message_info_t * message_info,
  rmw_subscription_allocation_t * allocation)
{
  using namespace rmw_robotops;

  if (underlying_rmw_take_with_info == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // STEP 1: Take the real message
  rmw_ret_t ret = underlying_rmw_take_with_info(
    subscription, ros_message, taken, message_info, allocation);

  // STEP 2: Handle tracing (same as rmw_take, but with message_info available)
  // TODO(ROB-55): Use message_info to get publisher_gid, source_timestamp, etc.

  if (ret == RMW_RET_OK && *taken && is_tracing_enabled()) {
    try {
      // For now, use same logic as rmw_take
      // In future, extract more metadata from message_info
      TraceContext extracted_context;
      bool context_found = extract_trace_context_from_dds(nullptr, extracted_context);

      if (context_found && !extracted_context.is_empty()) {
        TraceContext new_context;
        std::strncpy(new_context.trace_id, extracted_context.trace_id, TRACE_ID_LENGTH);
        new_context.trace_id[TRACE_ID_LENGTH] = '\0';
        generate_span_id(new_context.span_id);
        std::strncpy(
          new_context.parent_span_id,
          extracted_context.span_id,
          SPAN_ID_LENGTH);
        new_context.parent_span_id[SPAN_ID_LENGTH] = '\0';
        set_trace_context(new_context);
      } else {
        TraceContext new_context = TraceContext::empty();
        generate_trace_id(new_context.trace_id);
        generate_span_id(new_context.span_id);
        new_context.parent_span_id[0] = '\0';
        set_trace_context(new_context);
      }

      TraceEvent event;
      event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

      TraceContext current = get_trace_context();
      std::strncpy(event.trace_id, current.trace_id, TRACE_ID_LENGTH);
      event.trace_id[TRACE_ID_LENGTH] = '\0';
      std::strncpy(event.span_id, current.span_id, SPAN_ID_LENGTH);
      event.span_id[SPAN_ID_LENGTH] = '\0';
      std::strncpy(event.parent_span_id, current.parent_span_id, SPAN_ID_LENGTH);
      event.parent_span_id[SPAN_ID_LENGTH] = '\0';

      event.operation = OP_SUBSCRIBE;
      std::strncpy(
        event.topic_or_service,
        subscription->topic_name,
        MAX_TOPIC_NAME_LENGTH - 1);
      event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

      if (!get_trace_event_queue().try_push(event)) {
        record_trace_failure();
      } else {
        record_trace_success();
      }
    } catch (...) {
      record_trace_failure();
    }
  }

  return ret;
}

}  // extern "C"
