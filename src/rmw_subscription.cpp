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
#include "rmw_robotops/correlation_strategy.hpp"
#include "rmw_robotops/dds_metadata.hpp"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
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

/// Global correlation strategy (lazy initialized)
std::unique_ptr<rmw_robotops::CorrelationStrategy> & get_correlation_strategy() noexcept
{
  static std::unique_ptr<rmw_robotops::CorrelationStrategy> strategy;
  if (!strategy) {
    strategy = rmw_robotops::create_correlation_strategy();
  }
  return strategy;
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
  // Delegate to rmw_take_with_info with a local message_info
  // This ensures consistent tracing behavior for both API variants
  rmw_message_info_t message_info;
  rmw_ret_t ret = rmw_take_with_info(
    subscription, ros_message, taken, &message_info, allocation);
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
  using rmw_robotops::compute_content_hash;
  using rmw_robotops::generate_span_id;
  using rmw_robotops::get_dds_domain_id;
  using rmw_robotops::get_trace_event_queue;
  using rmw_robotops::is_tracing_enabled;
  using rmw_robotops::record_trace_failure;
  using rmw_robotops::record_trace_success;
  using rmw_robotops::set_trace_context;
  using rmw_robotops::TraceContext;
  using rmw_robotops::TraceEvent;

  if (underlying_rmw_take_with_info == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Get correlation strategy and tracing state (lazy init, outside critical path)
  auto & strategy = get_correlation_strategy();
  bool tracing_active = is_tracing_enabled();
  char span_id_buf[17] = {0};

  // STEP 1: Emit START event (BEFORE underlying take)
  if (tracing_active) {
    try {
      generate_span_id(span_id_buf);

      // Emit LTTng tracepoint
      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, take_rmw_start, subscription);
      #endif
    } catch (...) {
      record_trace_failure();
      tracing_active = false;  // Disable for this take
    }
  }

  // STEP 2: REAL MESSAGE FIRST (Safety guarantee - never blocked by tracing)
  rmw_ret_t ret = underlying_rmw_take_with_info(
    subscription, ros_message, taken, message_info, allocation);

  // STEP 3: Emit END event (AFTER underlying take, only if message was taken)
  if (ret == RMW_RET_OK && *taken && tracing_active) {
    try {
      // Extract correlation metadata using strategy
      TraceContext extracted_context;
      rmw_robotops::CorrelationMetadata correlation_metadata;

      // Pass DDS-specific sample info if available
      // (rmw_fastrtps stores it in implementation_identifier)
      // For now, use nullptr - strategy will use message_info for fallback
      const void * dds_sample_info = nullptr;

      // NOTE: We don't have direct access to the serialized CDR buffer here
      // because the underlying RMW handles deserialization internally.
      // Estimate message size based on typical ROS message sizes (1KB average).
      // True serialized size requires DDS-level interception (ROB-106).
      size_t estimated_msg_size = 1024;  // Reasonable default for correlation

      bool context_extracted = strategy->extract_context(
        subscription,
        message_info,
        dds_sample_info,
        ros_message,
        estimated_msg_size,
        extracted_context,
        correlation_metadata);

      // Determine trace context for this span
      TraceContext new_context;
      if (context_extracted && !extracted_context.is_empty()) {
        // Continue existing trace
        std::memcpy(new_context.trace_id, extracted_context.trace_id,
            sizeof(new_context.trace_id) - 1);
        new_context.trace_id[sizeof(new_context.trace_id) - 1] = '\0';

        std::memcpy(new_context.span_id, span_id_buf, sizeof(new_context.span_id) - 1);
        new_context.span_id[sizeof(new_context.span_id) - 1] = '\0';

        std::memcpy(new_context.parent_span_id, extracted_context.span_id,
            sizeof(new_context.parent_span_id) - 1);
        new_context.parent_span_id[sizeof(new_context.parent_span_id) - 1] = '\0';
      } else {
        // No context found - this is a root span (start of new trace)
        // Mint new trace_id
        rmw_robotops::generate_trace_id(new_context.trace_id);

        std::memcpy(new_context.span_id, span_id_buf, sizeof(new_context.span_id) - 1);
        new_context.span_id[sizeof(new_context.span_id) - 1] = '\0';

        new_context.parent_span_id[0] = '\0';  // Root span
      }

      // Set trace context for downstream propagation
      set_trace_context(new_context);

      // Compute content hash if needed (for fallback correlation or verification)
      uint64_t content_hash = 0;
      if (!strategy->is_deterministic()) {
        // NOTE: We don't have direct access to the serialized CDR buffer.
        // Use message pointer + timestamp from message_info as a proxy.
        // This matches the publisher-side hashing for correlation.
        uint64_t timestamp_ns = 0;
        if (message_info != nullptr) {
          timestamp_ns = message_info->source_timestamp;
        }

        uint64_t composite = reinterpret_cast<uintptr_t>(ros_message) ^ timestamp_ns;
        content_hash = compute_content_hash(&composite, sizeof(composite));
      }

      // Emit LTTng tracepoint
      #ifdef ROS_TRACING_ENABLED
      tracepoint(
        robotops, take_rmw_end,
        ros_message,
        new_context.trace_id, span_id_buf,
        correlation_metadata.publisher_gid,
        correlation_metadata.source_timestamp_ns,
        content_hash);
      #endif

      // Emit START TraceEvent for robot_agent
      TraceEvent start_event;
      start_event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
      start_event.event_type = robotops_msgs__msg__TraceEvent__EVENT_TAKE_RMW_START;

      // Copy strings with explicit null termination
      std::memcpy(start_event.trace_id, new_context.trace_id, sizeof(start_event.trace_id) - 1);
      start_event.trace_id[sizeof(start_event.trace_id) - 1] = '\0';

      std::memcpy(start_event.span_id, span_id_buf, sizeof(start_event.span_id) - 1);
      start_event.span_id[sizeof(start_event.span_id) - 1] = '\0';

      std::memcpy(start_event.parent_span_id, new_context.parent_span_id,
          sizeof(start_event.parent_span_id) - 1);
      start_event.parent_span_id[sizeof(start_event.parent_span_id) - 1] = '\0';

      size_t topic_len = std::min(std::strlen(subscription->topic_name),
          sizeof(start_event.topic_or_service) - 1);
      std::memcpy(start_event.topic_or_service, subscription->topic_name, topic_len);
      start_event.topic_or_service[topic_len] = '\0';

      start_event.msg_ptr = reinterpret_cast<uint64_t>(ros_message);
      start_event.content_hash = content_hash;
      start_event.dds_domain_id = get_dds_domain_id();
      start_event.correlation_method = strategy->get_correlation_method();

      // Correlation metadata from DDS
      size_t gid_len = std::min(correlation_metadata.publisher_gid.length(),
          sizeof(start_event.publisher_gid) - 1);
      std::memcpy(start_event.publisher_gid, correlation_metadata.publisher_gid.c_str(), gid_len);
      start_event.publisher_gid[gid_len] = '\0';

      start_event.sequence_number = correlation_metadata.sequence_number;
      start_event.source_timestamp_ns = correlation_metadata.source_timestamp_ns;

      // Get subscription metadata
      SubscriptionMetadata metadata;
      if (get_subscription_metadata(subscription, metadata)) {
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
      end_event.event_type = robotops_msgs__msg__TraceEvent__EVENT_TAKE_RMW_END;

      // Push events to queue (non-blocking)
      if (!get_trace_event_queue().try_push(start_event) ||
        !get_trace_event_queue().try_push(end_event))
      {
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
