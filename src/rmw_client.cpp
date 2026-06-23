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
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/trace_context_change_publisher.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "rmw_robotops/utils.hpp"
#include "robotops_msgs/msg/trace_event.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// LTTng tracepoints (optional)
#ifdef ROS_TRACING_ENABLED
#define TRACEPOINT_DEFINE
#include "rmw_robotops/tp_call.h"
#endif

// Forward declaration of underlying RMW functions
extern "C" {
extern rmw_client_t * (* underlying_rmw_create_client)(
  const rmw_node_t *, const rosidl_service_type_support_t *,
  const char *, const rmw_qos_profile_t *);
extern rmw_ret_t (* underlying_rmw_destroy_client)(
  rmw_node_t *, rmw_client_t *);
extern rmw_ret_t (* underlying_rmw_send_request)(
  const rmw_client_t *, const void *, int64_t *);
extern rmw_ret_t (* underlying_rmw_take_response)(
  const rmw_client_t *, rmw_service_info_t *, void *, bool *);
}

namespace
{

using rmw_robotops::MAX_NODE_NAME_LENGTH;
using rmw_robotops::MAX_TOPIC_NAME_LENGTH;

/// Metadata about a client (stored at creation time)
struct ClientMetadata
{
  char node_name[MAX_NODE_NAME_LENGTH];
  char node_namespace[MAX_NODE_NAME_LENGTH];
  char service_name[MAX_TOPIC_NAME_LENGTH];  // Services use same length as topics
  // Cached introspection members for content hashing (ROB-406). nullptr if the
  // service's package was built without rosidl_typesupport_introspection_c.
  const rosidl_typesupport_introspection_c__MessageMembers * request_members;
  const rosidl_typesupport_introspection_c__MessageMembers * response_members;
};

/// Pending request context (for response correlation)
struct PendingRequest
{
  char trace_id[rmw_robotops::TRACE_ID_LENGTH + 1];
  char span_id[rmw_robotops::SPAN_ID_LENGTH + 1];
  uint64_t timestamp_ns;
};

/// Cache of client metadata
std::unordered_map<const rmw_client_t *, ClientMetadata> client_metadata_cache;
std::mutex client_metadata_mutex;

/// Cache of pending requests (keyed by sequence_id)
std::unordered_map<int64_t, PendingRequest> pending_requests;
std::mutex pending_requests_mutex;

/// Store metadata for a client
void store_client_metadata(
  const rmw_client_t * client,
  const rmw_node_t * node,
  const rosidl_service_type_support_t * type_support,
  const char * service_name) noexcept
{
  try {
    ClientMetadata metadata;

    size_t name_len = std::min(std::strlen(node->name), MAX_NODE_NAME_LENGTH - 1);
    std::memcpy(metadata.node_name, node->name, name_len);
    metadata.node_name[name_len] = '\0';

    size_t ns_len = std::min(std::strlen(node->namespace_), MAX_NODE_NAME_LENGTH - 1);
    std::memcpy(metadata.node_namespace, node->namespace_, ns_len);
    metadata.node_namespace[ns_len] = '\0';

    size_t svc_len = std::min(std::strlen(service_name), MAX_TOPIC_NAME_LENGTH - 1);
    std::memcpy(metadata.service_name, service_name, svc_len);
    metadata.service_name[svc_len] = '\0';

    // Cache introspection members for content hashing (ROB-406)
    metadata.request_members =
      rmw_robotops::get_service_request_members(type_support);
    metadata.response_members =
      rmw_robotops::get_service_response_members(type_support);

    std::lock_guard<std::mutex> lock(client_metadata_mutex);
    client_metadata_cache[client] = metadata;
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

/// Retrieve metadata for a client
bool get_client_metadata(
  const rmw_client_t * client,
  ClientMetadata & metadata) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(client_metadata_mutex);
    auto it = client_metadata_cache.find(client);
    if (it != client_metadata_cache.end()) {
      metadata = it->second;
      return true;
    }
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
  return false;
}

/// Remove metadata for a client
void remove_client_metadata(const rmw_client_t * client) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(client_metadata_mutex);
    client_metadata_cache.erase(client);
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

}  // anonymous namespace

extern "C"
{
rmw_client_t *
rmw_create_client(
  const rmw_node_t * node,
  const rosidl_service_type_support_t * type_support,
  const char * service_name,
  const rmw_qos_profile_t * qos_profile)
{
  if (underlying_rmw_create_client == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return nullptr;
  }

  // Create client using underlying RMW
  rmw_client_t * client = underlying_rmw_create_client(
    node, type_support, service_name, qos_profile);

  // Store metadata for later use
  if (client != nullptr) {
    store_client_metadata(client, node, type_support, service_name);
  }

  return client;
}

rmw_ret_t
rmw_destroy_client(rmw_node_t * node, rmw_client_t * client)
{
  if (underlying_rmw_destroy_client == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Remove metadata from cache before destroying
  remove_client_metadata(client);

  return underlying_rmw_destroy_client(node, client);
}

rmw_ret_t
rmw_send_request(
  const rmw_client_t * client,
  const void * ros_request,
  int64_t * sequence_id)
{
  using rmw_robotops::generate_span_id;
  using rmw_robotops::get_dds_domain_id;
  using rmw_robotops::get_or_mint_trace_context;
  using rmw_robotops::get_trace_event_queue;
  using rmw_robotops::is_tracing_enabled;
  using rmw_robotops::record_trace_failure;
  using rmw_robotops::record_trace_success;
  using rmw_robotops::TraceContext;
  using rmw_robotops::TraceEvent;

  if (underlying_rmw_send_request == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Get trace context (lazy init)
  TraceContext context;
  bool tracing_active = is_tracing_enabled();
  char span_id_buf[17] = {0};
  uint64_t start_timestamp_ns = 0;

  // STEP 1: Emit START event (BEFORE underlying send)
  if (tracing_active) {
    try {
      context = get_or_mint_trace_context();
      generate_span_id(span_id_buf);
      start_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

      // Note: We do NOT inject into DDS metadata for services
      // The related_sample_identity is reserved for DDS-RPC request-response correlation

      #ifdef ROS_TRACING_ENABLED
      tracepoint(
        robotops, send_request_start,
        client, context.trace_id, span_id_buf);
      #endif
    } catch (...) {
      record_trace_failure();
      tracing_active = false;
    }
  }

  // STEP 2: REAL REQUEST FIRST (Safety guarantee)
  rmw_ret_t ret = underlying_rmw_send_request(client, ros_request, sequence_id);

  // STEP 3: Emit END event and store pending request context
  if (ret == RMW_RET_OK && tracing_active) {
    try {
      // Store pending request for response correlation
      if (sequence_id != nullptr) {
        PendingRequest pending;
        std::memcpy(pending.trace_id, context.trace_id, sizeof(pending.trace_id) - 1);
        pending.trace_id[sizeof(pending.trace_id) - 1] = '\0';
        std::memcpy(pending.span_id, span_id_buf, sizeof(pending.span_id) - 1);
        pending.span_id[sizeof(pending.span_id) - 1] = '\0';
        pending.timestamp_ns = start_timestamp_ns;

        std::lock_guard<std::mutex> lock(pending_requests_mutex);
        pending_requests[*sequence_id] = pending;
      }

      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, send_request_end, client, true);
      #endif

      // Emit service request TraceEvent
      TraceEvent event;
      event.timestamp_ns = start_timestamp_ns;
      event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;

      std::memcpy(event.trace_id, context.trace_id, sizeof(event.trace_id) - 1);
      event.trace_id[sizeof(event.trace_id) - 1] = '\0';

      std::memcpy(event.span_id, span_id_buf, sizeof(event.span_id) - 1);
      event.span_id[sizeof(event.span_id) - 1] = '\0';

      std::memcpy(event.parent_span_id, context.parent_span_id,
          sizeof(event.parent_span_id) - 1);
      event.parent_span_id[sizeof(event.parent_span_id) - 1] = '\0';

      event.dds_domain_id = get_dds_domain_id();
      // ROB-406: content-hash correlation. This client send_request is the
      // PRODUCER end of the request leg; the service's rmw_take_request
      // (CONSUMER) correlates against it by the request payload's content_hash.
      event.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;
      event.direction = robotops_msgs__msg__TraceEvent__DIRECTION_PRODUCER;

      if (sequence_id != nullptr) {
        event.sequence_number = static_cast<uint64_t>(*sequence_id);
      }
      // Seed the correlation timestamp bucket with this send time so the
      // service-take (which uses the DDS source_timestamp of the same sample)
      // buckets together.
      event.source_timestamp_ns = static_cast<int64_t>(start_timestamp_ns);

      // Get client metadata
      ClientMetadata metadata;
      if (get_client_metadata(client, metadata)) {
        if (metadata.request_members != nullptr) {
          event.content_hash = rmw_robotops::compute_message_hash(
            ros_request, metadata.request_members);
        }

        size_t svc_len = std::min(std::strlen(metadata.service_name),
            sizeof(event.topic_or_service) - 1);
        std::memcpy(event.topic_or_service, metadata.service_name, svc_len);
        event.topic_or_service[svc_len] = '\0';

        size_t node_name_len = std::min(std::strlen(metadata.node_name),
            sizeof(event.node_name) - 1);
        std::memcpy(event.node_name, metadata.node_name, node_name_len);
        event.node_name[node_name_len] = '\0';

        size_t node_ns_len = std::min(std::strlen(metadata.node_namespace),
            sizeof(event.node_namespace) - 1);
        std::memcpy(event.node_namespace, metadata.node_namespace, node_ns_len);
        event.node_namespace[node_ns_len] = '\0';
      }

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

rmw_ret_t
rmw_take_response(
  const rmw_client_t * client,
  rmw_service_info_t * request_header,
  void * ros_response,
  bool * taken)
{
  using rmw_robotops::generate_span_id;
  using rmw_robotops::get_dds_domain_id;
  using rmw_robotops::get_trace_event_queue;
  using rmw_robotops::is_tracing_enabled;
  using rmw_robotops::record_trace_failure;
  using rmw_robotops::record_trace_success;
  using rmw_robotops::set_trace_context;
  using rmw_robotops::TraceContext;
  using rmw_robotops::TraceEvent;

  if (underlying_rmw_take_response == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  bool tracing_active = is_tracing_enabled();
  char span_id_buf[17] = {0};

  // STEP 1: Emit START event (BEFORE underlying take)
  if (tracing_active) {
    try {
      generate_span_id(span_id_buf);

      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, take_response_start, client);
      #endif
    } catch (...) {
      record_trace_failure();
      tracing_active = false;
    }
  }

  // STEP 2: REAL RESPONSE FIRST
  rmw_ret_t ret = underlying_rmw_take_response(
    client, request_header, ros_response, taken);

  // STEP 3: Emit END event and correlate with pending request
  if (ret == RMW_RET_OK && *taken && tracing_active) {
    try {
      // Look up pending request by sequence_id
      PendingRequest pending;
      bool found_pending = false;

      if (request_header != nullptr) {
        std::lock_guard<std::mutex> lock(pending_requests_mutex);
        auto it = pending_requests.find(request_header->request_id.sequence_number);
        if (it != pending_requests.end()) {
          pending = it->second;
          found_pending = true;
          pending_requests.erase(it);  // Clean up
        }
      }

      // Set trace context (continue from request or mint new)
      TraceContext new_context;
      if (found_pending) {
        std::memcpy(new_context.trace_id, pending.trace_id, sizeof(new_context.trace_id) - 1);
        new_context.trace_id[sizeof(new_context.trace_id) - 1] = '\0';

        std::memcpy(new_context.span_id, span_id_buf, sizeof(new_context.span_id) - 1);
        new_context.span_id[sizeof(new_context.span_id) - 1] = '\0';

        std::memcpy(new_context.parent_span_id, pending.span_id,
            sizeof(new_context.parent_span_id) - 1);
        new_context.parent_span_id[sizeof(new_context.parent_span_id) - 1] = '\0';
      } else {
        // No pending request found - mint new trace
        rmw_robotops::generate_trace_id(new_context.trace_id);
        std::memcpy(new_context.span_id, span_id_buf, sizeof(new_context.span_id) - 1);
        new_context.span_id[sizeof(new_context.span_id) - 1] = '\0';
        new_context.parent_span_id[0] = '\0';
      }

      set_trace_context(new_context);

      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, take_response_end, ros_response, new_context.trace_id, span_id_buf);
      #endif

      // Emit service response TraceEvent
      TraceEvent event;
      event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
      event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_RESPONSE;

      std::memcpy(event.trace_id, new_context.trace_id, sizeof(event.trace_id) - 1);
      event.trace_id[sizeof(event.trace_id) - 1] = '\0';

      std::memcpy(event.span_id, span_id_buf, sizeof(event.span_id) - 1);
      event.span_id[sizeof(event.span_id) - 1] = '\0';

      std::memcpy(event.parent_span_id, new_context.parent_span_id,
          sizeof(event.parent_span_id) - 1);
      event.parent_span_id[sizeof(event.parent_span_id) - 1] = '\0';

      event.dds_domain_id = get_dds_domain_id();
      // ROB-406: this client take_response is the CONSUMER end of the response
      // leg. The client's local trace is already continued via the
      // sequence_id-keyed pending-request lookup above (intra-process), so we
      // keep that trace_id/parent. We additionally tag direction + content_hash
      // so the response leg can be cross-process correlated to the server's
      // send_response in a later phase; for Phase 1 it is informational.
      event.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;
      event.direction = robotops_msgs__msg__TraceEvent__DIRECTION_CONSUMER;

      if (request_header != nullptr) {
        event.sequence_number = request_header->request_id.sequence_number;
        event.source_timestamp_ns =
          static_cast<int64_t>(request_header->source_timestamp);
      }

      // Get client metadata
      ClientMetadata metadata;
      if (get_client_metadata(client, metadata)) {
        if (metadata.response_members != nullptr) {
          event.content_hash = rmw_robotops::compute_message_hash(
            ros_response, metadata.response_members);
        }

        size_t svc_len = std::min(std::strlen(metadata.service_name),
            sizeof(event.topic_or_service) - 1);
        std::memcpy(event.topic_or_service, metadata.service_name, svc_len);
        event.topic_or_service[svc_len] = '\0';

        size_t node_name_len = std::min(std::strlen(metadata.node_name),
            sizeof(event.node_name) - 1);
        std::memcpy(event.node_name, metadata.node_name, node_name_len);
        event.node_name[node_name_len] = '\0';

        size_t node_ns_len = std::min(std::strlen(metadata.node_namespace),
            sizeof(event.node_namespace) - 1);
        std::memcpy(event.node_namespace, metadata.node_namespace, node_ns_len);
        event.node_namespace[node_ns_len] = '\0';
      }

      if (!get_trace_event_queue().try_push(event)) {
        record_trace_failure();
      } else {
        record_trace_success();
      }

      // Emit CONTEXT_ENTERED so robot_agent can correlate /rosout logs with this trace (ROB-179)
      {
        rmw_robotops::TraceContextChangeEvent ctx_event;
        ctx_event.timestamp_ns = event.timestamp_ns;
        ctx_event.thread_id = rmw_robotops::get_current_thread_id();
        ctx_event.change_type = rmw_robotops::CONTEXT_CHANGE_ENTERED;
        std::memcpy(ctx_event.trace_id, new_context.trace_id,
          sizeof(ctx_event.trace_id) - 1);
        ctx_event.trace_id[sizeof(ctx_event.trace_id) - 1] = '\0';
        std::memcpy(ctx_event.span_id, new_context.span_id,
          sizeof(ctx_event.span_id) - 1);
        ctx_event.span_id[sizeof(ctx_event.span_id) - 1] = '\0';
        std::memcpy(ctx_event.node_name, event.node_name,
          sizeof(ctx_event.node_name) - 1);
        ctx_event.node_name[sizeof(ctx_event.node_name) - 1] = '\0';
        std::memcpy(ctx_event.node_namespace, event.node_namespace,
          sizeof(ctx_event.node_namespace) - 1);
        ctx_event.node_namespace[sizeof(ctx_event.node_namespace) - 1] = '\0';
        rmw_robotops::enqueue_context_change(ctx_event);
      }
    } catch (...) {
      record_trace_failure();
    }
  }

  return ret;
}
}  // extern "C"
