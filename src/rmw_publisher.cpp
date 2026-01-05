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
#include <mutex>
#include <unordered_map>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw_robotops/config.hpp"
#include "rmw_robotops/dds_metadata.hpp"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

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

namespace
{

using namespace rmw_robotops;

/// Metadata about a publisher (stored at creation time)
struct PublisherMetadata
{
  char node_name[MAX_NODE_NAME_LENGTH];
  char node_namespace[MAX_NODE_NAME_LENGTH];
  char message_type[MAX_MESSAGE_TYPE_LENGTH];
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

    // Store message type name from type support
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

      // Copy trace context (using memcpy for fixed-length hex strings)
      std::memcpy(event.trace_id, context.trace_id, TRACE_ID_LENGTH);
      event.trace_id[TRACE_ID_LENGTH] = '\0';
      std::memcpy(event.span_id, context.span_id, SPAN_ID_LENGTH);
      event.span_id[SPAN_ID_LENGTH] = '\0';
      std::memcpy(event.parent_span_id, context.parent_span_id, SPAN_ID_LENGTH);
      event.parent_span_id[SPAN_ID_LENGTH] = '\0';

      // No span links for publish operations (used for fan-in scenarios)
      event.span_link_count = 0;

      // Event details
      event.operation = OP_PUBLISH;
      std::strncpy(
        event.topic_or_service,
        publisher->topic_name,
        MAX_TOPIC_NAME_LENGTH - 1);
      event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

      // Retrieve publisher metadata (node name, namespace, message type)
      PublisherMetadata metadata;
      if (get_publisher_metadata(publisher, metadata)) {
        // Use strlcpy-style pattern: copy at most size-1, then null-terminate
        size_t node_name_len = std::min(std::strlen(metadata.node_name), MAX_NODE_NAME_LENGTH - 1);
        std::memcpy(event.node_name, metadata.node_name, node_name_len);
        event.node_name[node_name_len] = '\0';

        size_t node_ns_len = std::min(std::strlen(metadata.node_namespace), MAX_NODE_NAME_LENGTH - 1);
        std::memcpy(event.node_namespace, metadata.node_namespace, node_ns_len);
        event.node_namespace[node_ns_len] = '\0';

        size_t msg_type_len = std::min(std::strlen(metadata.message_type), MAX_MESSAGE_TYPE_LENGTH - 1);
        std::memcpy(event.message_type, metadata.message_type, msg_type_len);
        event.message_type[msg_type_len] = '\0';
      } else {
        // Metadata not found - leave fields empty
        event.node_name[0] = '\0';
        event.node_namespace[0] = '\0';
        event.message_type[0] = '\0';
      }

      // Initialize remaining metadata fields
      // TODO(ROB-55): Extract from DDS (requires FastDDS integration)
      // - publisher_gid: Unique identifier for publisher instance
      // - sequence_number: Message sequence number from DDS
      event.publisher_gid[0] = '\0';
      event.sequence_number = 0;

      // TODO(ROB-55): Measure serialized message size
      // - message_size_bytes: Actual bytes on the wire
      // Requires serialization, which has overhead - consider if needed
      event.message_size_bytes = 0;

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
