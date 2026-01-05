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

namespace
{

using rmw_robotops::MAX_MESSAGE_TYPE_LENGTH;
using rmw_robotops::MAX_NODE_NAME_LENGTH;

/// Metadata about a subscription (stored at creation time)
struct SubscriptionMetadata
{
  char node_name[MAX_NODE_NAME_LENGTH];
  char node_namespace[MAX_NODE_NAME_LENGTH];
  char message_type[MAX_MESSAGE_TYPE_LENGTH];
};

/// Cache of subscription metadata (keyed by subscription pointer)
std::unordered_map<const rmw_subscription_t *, SubscriptionMetadata> subscription_metadata_cache;
std::mutex subscription_metadata_mutex;

/// Store metadata for a subscription
void store_subscription_metadata(
  const rmw_subscription_t * subscription,
  const rmw_node_t * node,
  const rosidl_message_type_support_t * type_support) noexcept
{
  try {
    SubscriptionMetadata metadata;

    // Store node name and namespace
    size_t name_len = std::min(std::strlen(node->name), MAX_NODE_NAME_LENGTH - 1);
    std::memcpy(metadata.node_name, node->name, name_len);
    metadata.node_name[name_len] = '\0';

    size_t ns_len = std::min(std::strlen(node->namespace_), MAX_NODE_NAME_LENGTH - 1);
    std::memcpy(metadata.node_namespace, node->namespace_, ns_len);
    metadata.node_namespace[ns_len] = '\0';

    // Store message type name from type support
    if (type_support != nullptr && type_support->data != nullptr) {
      const rosidl_message_type_support_t * ts =
        get_message_typesupport_handle(
          type_support,
          rosidl_typesupport_introspection_c__identifier);

      if (ts != nullptr) {
        const auto * members =
          static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(ts->data);
        if (members != nullptr) {
          snprintf(
            metadata.message_type,
            MAX_MESSAGE_TYPE_LENGTH,
            "%s/%s",
            members->message_namespace_,
            members->message_name_);
        }
      }
    }

    // Store in cache
    {
      std::lock_guard<std::mutex> lock(subscription_metadata_mutex);
      subscription_metadata_cache[subscription] = metadata;
    }
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

/// Retrieve metadata for a subscription
bool get_subscription_metadata(
  const rmw_subscription_t * subscription,
  SubscriptionMetadata & metadata) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(subscription_metadata_mutex);
    auto it = subscription_metadata_cache.find(subscription);
    if (it != subscription_metadata_cache.end()) {
      metadata = it->second;
      return true;
    }
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
  return false;
}

/// Remove metadata for a subscription
void remove_subscription_metadata(const rmw_subscription_t * subscription) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(subscription_metadata_mutex);
    subscription_metadata_cache.erase(subscription);
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

}  // anonymous namespace

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
  if (underlying_rmw_create_subscription == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return nullptr;
  }

  // Create subscription using underlying RMW
  rmw_subscription_t * subscription = underlying_rmw_create_subscription(
    node, type_support, topic_name, qos_profile, subscription_options);

  // Store metadata for later use in rmw_take
  if (subscription != nullptr) {
    store_subscription_metadata(subscription, node, type_support);
  }

  return subscription;
}

rmw_ret_t
rmw_destroy_subscription(rmw_node_t * node, rmw_subscription_t * subscription)
{
  if (underlying_rmw_destroy_subscription == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Remove metadata from cache before destroying
  remove_subscription_metadata(subscription);

  return underlying_rmw_destroy_subscription(node, subscription);
}

rmw_ret_t
rmw_take(
  const rmw_subscription_t * subscription,
  void * ros_message,
  bool * taken,
  rmw_subscription_allocation_t * allocation)
{
  using rmw_robotops::extract_trace_context_from_dds;
  using rmw_robotops::generate_span_id;
  using rmw_robotops::generate_trace_id;
  using rmw_robotops::get_trace_context;
  using rmw_robotops::get_trace_event_queue;
  using rmw_robotops::is_tracing_enabled;
  using rmw_robotops::MAX_MESSAGE_TYPE_LENGTH;
  using rmw_robotops::MAX_NODE_NAME_LENGTH;
  using rmw_robotops::MAX_TOPIC_NAME_LENGTH;
  using rmw_robotops::OP_SUBSCRIBE;
  using rmw_robotops::record_trace_failure;
  using rmw_robotops::record_trace_success;
  using rmw_robotops::set_trace_context;
  using rmw_robotops::SPAN_ID_LENGTH;
  using rmw_robotops::TRACE_ID_LENGTH;
  using rmw_robotops::TraceContext;
  using rmw_robotops::TraceEvent;

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
        std::memcpy(new_context.trace_id, extracted_context.trace_id, TRACE_ID_LENGTH);
        new_context.trace_id[TRACE_ID_LENGTH] = '\0';

        // Generate new span_id for this subscription event
        generate_span_id(new_context.span_id);

        // Set parent to the publisher's span
        std::memcpy(
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
      std::memcpy(event.trace_id, current.trace_id, TRACE_ID_LENGTH);
      event.trace_id[TRACE_ID_LENGTH] = '\0';
      std::memcpy(event.span_id, current.span_id, SPAN_ID_LENGTH);
      event.span_id[SPAN_ID_LENGTH] = '\0';
      std::memcpy(event.parent_span_id, current.parent_span_id, SPAN_ID_LENGTH);
      event.parent_span_id[SPAN_ID_LENGTH] = '\0';

      // No span links for subscribe operations
      event.span_link_count = 0;

      event.operation = OP_SUBSCRIBE;
      std::strncpy(
        event.topic_or_service,
        subscription->topic_name,
        MAX_TOPIC_NAME_LENGTH - 1);
      event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

      // Retrieve subscription metadata (node name, namespace, message type)
      SubscriptionMetadata metadata;
      if (get_subscription_metadata(subscription, metadata)) {
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
      // TODO(ROB-55): Extract from DDS message_info (requires FastDDS integration)
      // - publisher_gid: GID of publishing node
      // - sequence_number: Message sequence number
      event.publisher_gid[0] = '\0';
      event.sequence_number = 0;

      // TODO(ROB-55): Measure serialized message size
      event.message_size_bytes = 0;

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
  using rmw_robotops::extract_trace_context_from_dds;
  using rmw_robotops::generate_span_id;
  using rmw_robotops::generate_trace_id;
  using rmw_robotops::get_trace_context;
  using rmw_robotops::get_trace_event_queue;
  using rmw_robotops::is_tracing_enabled;
  using rmw_robotops::MAX_MESSAGE_TYPE_LENGTH;
  using rmw_robotops::MAX_NODE_NAME_LENGTH;
  using rmw_robotops::MAX_TOPIC_NAME_LENGTH;
  using rmw_robotops::OP_SUBSCRIBE;
  using rmw_robotops::record_trace_failure;
  using rmw_robotops::record_trace_success;
  using rmw_robotops::set_trace_context;
  using rmw_robotops::SPAN_ID_LENGTH;
  using rmw_robotops::TRACE_ID_LENGTH;
  using rmw_robotops::TraceContext;
  using rmw_robotops::TraceEvent;

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
        std::memcpy(new_context.trace_id, extracted_context.trace_id, TRACE_ID_LENGTH);
        new_context.trace_id[TRACE_ID_LENGTH] = '\0';
        generate_span_id(new_context.span_id);
        std::memcpy(
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
      std::memcpy(event.trace_id, current.trace_id, TRACE_ID_LENGTH);
      event.trace_id[TRACE_ID_LENGTH] = '\0';
      std::memcpy(event.span_id, current.span_id, SPAN_ID_LENGTH);
      event.span_id[SPAN_ID_LENGTH] = '\0';
      std::memcpy(event.parent_span_id, current.parent_span_id, SPAN_ID_LENGTH);
      event.parent_span_id[SPAN_ID_LENGTH] = '\0';

      // No span links for subscribe operations
      event.span_link_count = 0;

      event.operation = OP_SUBSCRIBE;
      std::strncpy(
        event.topic_or_service,
        subscription->topic_name,
        MAX_TOPIC_NAME_LENGTH - 1);
      event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

      // Retrieve subscription metadata (node name, namespace, message type)
      SubscriptionMetadata metadata;
      if (get_subscription_metadata(subscription, metadata)) {
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
      // TODO(ROB-55): Extract from message_info (requires FastDDS integration)
      // - publisher_gid: From message_info->publisher_gid
      // - sequence_number: From message_info or DDS
      event.publisher_gid[0] = '\0';
      event.sequence_number = 0;

      // TODO(ROB-55): Measure serialized message size
      event.message_size_bytes = 0;

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
