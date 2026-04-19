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

#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw_robotops/config.hpp"
#include "rmw_robotops/diagnostics_metrics.hpp"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/trace_context_change_publisher.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "rmw_robotops/utils.hpp"
#include "robotops_msgs/msg/trace_event.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// LTTng tracepoints (optional - gracefully degrades if not available)
#ifdef ROS_TRACING_ENABLED
#define TRACEPOINT_DEFINE
#include "rmw_robotops/tp_call.h"
#endif

// Forward declaration of underlying RMW functions
// These will be dynamically loaded from the underlying RMW implementation
extern "C" {
extern rmw_ret_t (* underlying_rmw_publish)(
  const rmw_publisher_t *, const void *, rmw_publisher_allocation_t *);
extern rmw_ret_t (* underlying_rmw_publish_serialized_message)(
  const rmw_publisher_t *, const rmw_serialized_message_t *, rmw_publisher_allocation_t *);
extern rmw_publisher_t * (* underlying_rmw_create_publisher)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_publisher_options_t *);
extern rmw_ret_t (* underlying_rmw_destroy_publisher)(
  rmw_node_t *, rmw_publisher_t *);
}

namespace
{

using rmw_robotops::MAX_MESSAGE_TYPE_LENGTH;
using rmw_robotops::MAX_NODE_NAME_LENGTH;

/// Metadata about a publisher (stored at creation time)
struct PublisherMetadata
{
  char node_name[MAX_NODE_NAME_LENGTH];
  char node_namespace[MAX_NODE_NAME_LENGTH];
  char message_type[MAX_MESSAGE_TYPE_LENGTH];
  const rosidl_typesupport_introspection_c__MessageMembers * members;
};

/// Cache of publisher metadata (keyed by publisher pointer)
/// Note: This is NOT in the hot path - lookups happen during publish,
///       but insertions/deletions only happen during create/destroy
std::unordered_map<const rmw_publisher_t *, PublisherMetadata> publisher_metadata_cache;
std::mutex publisher_metadata_mutex;

/// Store metadata for a publisher
void store_publisher_metadata(
  const rmw_publisher_t * publisher,
  const rmw_node_t * node,
  const rosidl_message_type_support_t * type_support) noexcept
{
  try {
    PublisherMetadata metadata;

    // Store node name and namespace
    size_t name_len = std::min(std::strlen(node->name), MAX_NODE_NAME_LENGTH - 1);
    std::memcpy(metadata.node_name, node->name, name_len);
    metadata.node_name[name_len] = '\0';

    size_t ns_len = std::min(std::strlen(node->namespace_), MAX_NODE_NAME_LENGTH - 1);
    std::memcpy(metadata.node_namespace, node->namespace_, ns_len);
    metadata.node_namespace[ns_len] = '\0';

    // Store message type name and introspection members from type support
    metadata.members = nullptr;
    if (type_support != nullptr && type_support->data != nullptr) {
      // Extract type name from type support (e.g., "std_msgs/msg/String")
      const rosidl_message_type_support_t * ts =
        get_message_typesupport_handle(
          type_support,
          rosidl_typesupport_introspection_c__identifier);

      if (ts != nullptr) {
        const auto * members =
          static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(ts->data);
        if (members != nullptr) {
          // Format: "package_name/msg/MessageName"
          snprintf(
            metadata.message_type,
            MAX_MESSAGE_TYPE_LENGTH,
            "%s/%s",
            members->message_namespace_,
            members->message_name_);
          // Cache members pointer for content hashing
          metadata.members = members;
        }
      }
    }

    // Store in cache (with mutex protection)
    {
      std::lock_guard<std::mutex> lock(publisher_metadata_mutex);
      publisher_metadata_cache[publisher] = metadata;
    }
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

/// Retrieve metadata for a publisher
bool get_publisher_metadata(
  const rmw_publisher_t * publisher,
  PublisherMetadata & metadata) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(publisher_metadata_mutex);
    auto it = publisher_metadata_cache.find(publisher);
    if (it != publisher_metadata_cache.end()) {
      metadata = it->second;
      return true;
    }
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
  return false;
}

/// Remove metadata for a publisher
void remove_publisher_metadata(const rmw_publisher_t * publisher) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(publisher_metadata_mutex);
    publisher_metadata_cache.erase(publisher);
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}


}  // anonymous namespace


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
  if (underlying_rmw_create_publisher == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return nullptr;
  }

  // Create publisher using underlying RMW
  rmw_publisher_t * publisher = underlying_rmw_create_publisher(
    node, type_support, topic_name, qos_profile, publisher_options);

  // Store metadata for later use in rmw_publish
  if (publisher != nullptr) {
    store_publisher_metadata(publisher, node, type_support);
  }

  return publisher;
}

rmw_ret_t
rmw_destroy_publisher(rmw_node_t * node, rmw_publisher_t * publisher)
{
  if (underlying_rmw_destroy_publisher == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Remove metadata from cache before destroying
  remove_publisher_metadata(publisher);

  return underlying_rmw_destroy_publisher(node, publisher);
}

rmw_ret_t
rmw_publish(
  const rmw_publisher_t * publisher,
  const void * ros_message,
  rmw_publisher_allocation_t * allocation)
{
  using rmw_robotops::compute_content_hash;
  using rmw_robotops::compute_message_hash;
  using rmw_robotops::generate_span_id;
  using rmw_robotops::get_dds_domain_id;
  using rmw_robotops::get_or_mint_trace_context;
  using rmw_robotops::get_trace_event_queue;
  using rmw_robotops::is_tracing_enabled;
  using rmw_robotops::record_trace_failure;
  using rmw_robotops::record_trace_success;
  using rmw_robotops::TraceContext;
  using rmw_robotops::TraceEvent;

  if (underlying_rmw_publish == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Get trace context (lazy init, outside critical path)
  TraceContext context;
  bool tracing_active = is_tracing_enabled();
  uint64_t content_hash = 0;
  char span_id_buf[17] = {0};

  // STEP 1: Emit START event (BEFORE underlying publish)
  if (tracing_active) {
    try {
      context = get_or_mint_trace_context();
      generate_span_id(span_id_buf);

      // Compute content hash using introspection (always computed for correlation)
      // Get cached introspection members for this publisher
      PublisherMetadata pub_metadata;
      bool have_metadata = get_publisher_metadata(publisher, pub_metadata);

      if (have_metadata && pub_metadata.members != nullptr) {
        content_hash = compute_message_hash(ros_message, pub_metadata.members);
      }

      // Emit LTTng tracepoint
      #ifdef ROS_TRACING_ENABLED
      tracepoint(
        robotops, publish_rmw_start,
        ros_message, publisher->topic_name,
        context.trace_id, span_id_buf, context.parent_span_id,
        content_hash);
      #endif
    } catch (...) {
      record_trace_failure();
      tracing_active = false;  // Disable for this publish
    }
  }

  // STEP 2: REAL MESSAGE FIRST (Safety guarantee - never blocked by tracing)
  rmw_ret_t ret = underlying_rmw_publish(publisher, ros_message, allocation);

  // STEP 3: Emit END event (AFTER underlying publish)
  if (ret == RMW_RET_OK && tracing_active) {
    try {
      // Emit LTTng tracepoint
      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, publish_rmw_end, ros_message, true);
      #endif

      // Emit START TraceEvent for robot_agent
      TraceEvent start_event;
      start_event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

      // Detect action event type or use regular publish event
      start_event.event_type = rmw_robotops::detect_action_event_type(
        publisher->topic_name, true, true);

      // Copy strings with explicit null termination
      std::memcpy(start_event.trace_id, context.trace_id, sizeof(start_event.trace_id) - 1);
      start_event.trace_id[sizeof(start_event.trace_id) - 1] = '\0';

      std::memcpy(start_event.span_id, span_id_buf, sizeof(start_event.span_id) - 1);
      start_event.span_id[sizeof(start_event.span_id) - 1] = '\0';

      std::memcpy(start_event.parent_span_id, context.parent_span_id,
          sizeof(start_event.parent_span_id) - 1);
      start_event.parent_span_id[sizeof(start_event.parent_span_id) - 1] = '\0';

      size_t topic_len = std::min(std::strlen(publisher->topic_name),
          sizeof(start_event.topic_or_service) - 1);
      std::memcpy(start_event.topic_or_service, publisher->topic_name, topic_len);
      start_event.topic_or_service[topic_len] = '\0';

      start_event.msg_ptr = reinterpret_cast<uint64_t>(ros_message);
      start_event.content_hash = content_hash;
      start_event.dds_domain_id = get_dds_domain_id();
      start_event.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;

      // Collect pending contexts for span links (fan-in scenario)
      TraceContext pending_contexts[rmw_robotops::MAX_SPAN_LINKS];
      size_t num_pending = rmw_robotops::collect_pending_contexts(
        pending_contexts,
        rmw_robotops::MAX_SPAN_LINKS);

      start_event.span_link_count = num_pending;
      for (size_t i = 0; i < num_pending; ++i) {
        // Format as "trace_id:span_id" with explicit length limits for compiler
        snprintf(
          start_event.span_links[i],
          sizeof(start_event.span_links[i]),
          "%.*s:%.*s",
          static_cast<int>(rmw_robotops::TRACE_ID_LENGTH),
          pending_contexts[i].trace_id,
          static_cast<int>(rmw_robotops::SPAN_ID_LENGTH),
          pending_contexts[i].span_id);
      }

      // Get publisher metadata
      PublisherMetadata metadata;
      if (get_publisher_metadata(publisher, metadata)) {
        size_t node_name_len = std::min(std::strlen(metadata.node_name),
            sizeof(start_event.node_name) - 1);
        std::memcpy(start_event.node_name, metadata.node_name, node_name_len);
        start_event.node_name[node_name_len] = '\0';

        size_t node_ns_len = std::min(std::strlen(metadata.node_namespace),
            sizeof(start_event.node_namespace) - 1);
        std::memcpy(start_event.node_namespace, metadata.node_namespace, node_ns_len);
        start_event.node_namespace[node_ns_len] = '\0';

        size_t msg_type_len = std::min(std::strlen(metadata.message_type),
            sizeof(start_event.message_type) - 1);
        std::memcpy(start_event.message_type, metadata.message_type, msg_type_len);
        start_event.message_type[msg_type_len] = '\0';
      }

      // Emit END TraceEvent
      TraceEvent end_event = start_event;
      // Detect action event type or use regular publish END event
      end_event.event_type = rmw_robotops::detect_action_event_type(
        publisher->topic_name, true, false);

      // Push events to queue (non-blocking)
      if (!get_trace_event_queue().try_push(start_event) ||
        !get_trace_event_queue().try_push(end_event))
      {
        record_trace_failure();
        rmw_robotops::increment_traces_dropped();
        rmw_robotops::increment_traces_dropped();  // Two events dropped
      } else {
        record_trace_success();
        rmw_robotops::increment_traces_emitted();
        rmw_robotops::increment_traces_emitted();  // Two events emitted (start + end)
      }

      // Emit CONTEXT_EXITED — the callback that triggered this publish is done (ROB-179)
      {
        rmw_robotops::TraceContextChangeEvent ctx_event;
        ctx_event.timestamp_ns = start_event.timestamp_ns;
        ctx_event.thread_id = rmw_robotops::get_current_thread_id();
        ctx_event.change_type = rmw_robotops::CONTEXT_CHANGE_EXITED;
        std::memcpy(ctx_event.node_name, start_event.node_name,
          sizeof(ctx_event.node_name) - 1);
        ctx_event.node_name[sizeof(ctx_event.node_name) - 1] = '\0';
        std::memcpy(ctx_event.node_namespace, start_event.node_namespace,
          sizeof(ctx_event.node_namespace) - 1);
        ctx_event.node_namespace[sizeof(ctx_event.node_namespace) - 1] = '\0';
        // trace_id and span_id remain empty — not needed for CONTEXT_EXITED
        rmw_robotops::enqueue_context_change(ctx_event);
      }
    } catch (...) {
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
  using rmw_robotops::compute_content_hash;
  using rmw_robotops::generate_span_id;
  using rmw_robotops::get_dds_domain_id;
  using rmw_robotops::get_or_mint_trace_context;
  using rmw_robotops::get_trace_event_queue;
  using rmw_robotops::is_tracing_enabled;
  using rmw_robotops::record_trace_failure;
  using rmw_robotops::record_trace_success;
  using rmw_robotops::TraceContext;
  using rmw_robotops::TraceEvent;

  if (underlying_rmw_publish_serialized_message == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Get tracing state
  bool tracing_active = is_tracing_enabled();
  char span_id_buf[17] = {0};

  // STEP 1: Emit START event (BEFORE underlying publish)
  if (tracing_active) {
    try {
      generate_span_id(span_id_buf);

      // Emit LTTng tracepoint
      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, publish_rmw_start, serialized_message, publisher->topic_name,
        "", span_id_buf, "", 0);
      #endif
    } catch (...) {
      record_trace_failure();
      tracing_active = false;  // Disable for this publish
    }
  }

  // STEP 2: REAL MESSAGE FIRST (Safety guarantee - never blocked by tracing)
  rmw_ret_t ret = underlying_rmw_publish_serialized_message(
    publisher, serialized_message, allocation);

  // STEP 3: Emit END event (AFTER underlying publish)
  if (ret == RMW_RET_OK && tracing_active) {
    try {
      // Get or create trace context
      TraceContext context = get_or_mint_trace_context();

      // Compute content hash from serialized CDR buffer
      // Note: Serialized messages are already in CDR format, so we hash the buffer directly
      uint64_t content_hash = 0;
      if (serialized_message != nullptr && serialized_message->buffer != nullptr &&
        serialized_message->buffer_length > 0)
      {
        content_hash = compute_content_hash(
          serialized_message->buffer,
          serialized_message->buffer_length);
      }

      // Emit LTTng tracepoint
      #ifdef ROS_TRACING_ENABLED
      tracepoint(
        robotops, publish_rmw_end,
        serialized_message,
        context.trace_id, span_id_buf,
        content_hash);
      #endif

      // Emit START TraceEvent for robot_agent
      TraceEvent start_event;
      start_event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

      // Detect action event type or use regular publish event
      start_event.event_type = rmw_robotops::detect_action_event_type(
        publisher->topic_name, true, true);

      // Copy strings with explicit null termination
      std::memcpy(start_event.trace_id, context.trace_id, sizeof(start_event.trace_id) - 1);
      start_event.trace_id[sizeof(start_event.trace_id) - 1] = '\0';

      std::memcpy(start_event.span_id, span_id_buf, sizeof(start_event.span_id) - 1);
      start_event.span_id[sizeof(start_event.span_id) - 1] = '\0';

      std::memcpy(start_event.parent_span_id, context.parent_span_id,
          sizeof(start_event.parent_span_id) - 1);
      start_event.parent_span_id[sizeof(start_event.parent_span_id) - 1] = '\0';

      size_t topic_len = std::min(std::strlen(publisher->topic_name),
          sizeof(start_event.topic_or_service) - 1);
      std::memcpy(start_event.topic_or_service, publisher->topic_name, topic_len);
      start_event.topic_or_service[topic_len] = '\0';

      start_event.msg_ptr = reinterpret_cast<uint64_t>(serialized_message);
      start_event.content_hash = content_hash;
      start_event.dds_domain_id = get_dds_domain_id();
      start_event.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;

      // Get publisher metadata
      PublisherMetadata metadata;
      if (get_publisher_metadata(publisher, metadata)) {
        size_t node_name_len = std::min(std::strlen(metadata.node_name),
            sizeof(start_event.node_name) - 1);
        std::memcpy(start_event.node_name, metadata.node_name, node_name_len);
        start_event.node_name[node_name_len] = '\0';

        size_t node_ns_len = std::min(std::strlen(metadata.node_namespace),
            sizeof(start_event.node_namespace) - 1);
        std::memcpy(start_event.node_namespace, metadata.node_namespace, node_ns_len);
        start_event.node_namespace[node_ns_len] = '\0';

        size_t msg_type_len = std::min(std::strlen(metadata.message_type),
            sizeof(start_event.message_type) - 1);
        std::memcpy(start_event.message_type, metadata.message_type, msg_type_len);
        start_event.message_type[msg_type_len] = '\0';
      }

      // Emit END TraceEvent
      TraceEvent end_event = start_event;
      // Detect action event type or use regular publish END event
      end_event.event_type = rmw_robotops::detect_action_event_type(
        publisher->topic_name, true, false);

      // Push events to queue (non-blocking)
      if (!get_trace_event_queue().try_push(start_event) ||
        !get_trace_event_queue().try_push(end_event))
      {
        record_trace_failure();
        rmw_robotops::increment_traces_dropped();
        rmw_robotops::increment_traces_dropped();  // Two events dropped
      } else {
        record_trace_success();
        rmw_robotops::increment_traces_emitted();
        rmw_robotops::increment_traces_emitted();  // Two events emitted (start + end)
      }
    } catch (...) {
      record_trace_failure();
    }
  }

  return ret;
}
}  // extern "C"
