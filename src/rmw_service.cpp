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
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// LTTng tracepoints (optional)
#ifdef ROS_TRACING_ENABLED
#define TRACEPOINT_DEFINE
#include "rmw_robotops/tp_call.h"
#endif

// Forward declaration of underlying RMW functions
extern "C" {
extern rmw_service_t * (* underlying_rmw_create_service)(
  const rmw_node_t *, const rosidl_service_type_support_t *,
  const char *, const rmw_qos_profile_t *);
extern rmw_ret_t (* underlying_rmw_destroy_service)(
  rmw_node_t *, rmw_service_t *);
extern rmw_ret_t (* underlying_rmw_take_request)(
  const rmw_service_t *, rmw_service_info_t *, void *, bool *);
extern rmw_ret_t (* underlying_rmw_send_response)(
  const rmw_service_t *, rmw_request_id_t *, void *);
}

namespace
{

using rmw_robotops::MAX_NODE_NAME_LENGTH;
using rmw_robotops::MAX_TOPIC_NAME_LENGTH;

/// Metadata about a service (stored at creation time)
struct ServiceMetadata
{
  char node_name[MAX_NODE_NAME_LENGTH];
  char node_namespace[MAX_NODE_NAME_LENGTH];
  char service_name[MAX_TOPIC_NAME_LENGTH];  // Services use same length as topics
  // Cached introspection members for content hashing (ROB-406). nullptr if the
  // service's package was built without rosidl_typesupport_introspection_c.
  const rosidl_typesupport_introspection_c__MessageMembers * request_members;
  const rosidl_typesupport_introspection_c__MessageMembers * response_members;
};

/// Cache of service metadata
std::unordered_map<const rmw_service_t *, ServiceMetadata> service_metadata_cache;
std::mutex service_metadata_mutex;

/// Store metadata for a service
void store_service_metadata(
  const rmw_service_t * service,
  const rmw_node_t * node,
  const rosidl_service_type_support_t * type_support,
  const char * service_name) noexcept
{
  try {
    ServiceMetadata metadata;

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

    std::lock_guard<std::mutex> lock(service_metadata_mutex);
    service_metadata_cache[service] = metadata;
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

/// Retrieve metadata for a service
bool get_service_metadata(
  const rmw_service_t * service,
  ServiceMetadata & metadata) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(service_metadata_mutex);
    auto it = service_metadata_cache.find(service);
    if (it != service_metadata_cache.end()) {
      metadata = it->second;
      return true;
    }
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
  return false;
}

/// Remove metadata for a service
void remove_service_metadata(const rmw_service_t * service) noexcept
{
  try {
    std::lock_guard<std::mutex> lock(service_metadata_mutex);
    service_metadata_cache.erase(service);
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

}  // anonymous namespace

extern "C"
{
rmw_service_t *
rmw_create_service(
  const rmw_node_t * node,
  const rosidl_service_type_support_t * type_support,
  const char * service_name,
  const rmw_qos_profile_t * qos_profile)
{
  if (underlying_rmw_create_service == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return nullptr;
  }

  // Create service using underlying RMW
  rmw_service_t * service = underlying_rmw_create_service(
    node, type_support, service_name, qos_profile);

  // Store metadata for later use
  if (service != nullptr) {
    store_service_metadata(service, node, type_support, service_name);
  }

  return service;
}

rmw_ret_t
rmw_destroy_service(rmw_node_t * node, rmw_service_t * service)
{
  if (underlying_rmw_destroy_service == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Remove metadata from cache before destroying
  remove_service_metadata(service);

  return underlying_rmw_destroy_service(node, service);
}

rmw_ret_t
rmw_take_request(
  const rmw_service_t * service,
  rmw_service_info_t * request_header,
  void * ros_request,
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

  if (underlying_rmw_take_request == nullptr) {
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
      tracepoint(robotops, take_request_start, service);
      #endif
    } catch (...) {
      record_trace_failure();
      tracing_active = false;
    }
  }

  // STEP 2: REAL REQUEST FIRST
  rmw_ret_t ret = underlying_rmw_take_request(
    service, request_header, ros_request, taken);

  // STEP 3: Emit END event and set trace context
  if (ret == RMW_RET_OK && *taken && tracing_active) {
    try {
      // ROB-406: Do NOT mint a fresh trace_id here. Previously this path minted
      // a new root trace on every service take, guaranteeing every service
      // request rendered as a separate root span. Instead we leave trace_id and
      // parent_span_id EMPTY and emit a CONSUMER-direction content_hash so the
      // agent's CorrelationEngine can rewrite them to the client-send's trace —
      // exactly mirroring how the subscribe (rmw_take_with_info) path works.
      //
      // The local thread context still carries this span_id (with an empty
      // trace_id) so rmw_send_response on the same thread pairs its RESPONSE
      // event to this REQUEST via the shared span_id.
      TraceContext new_context;
      new_context.trace_id[0] = '\0';  // Filled in by the agent on correlation

      std::memcpy(new_context.span_id, span_id_buf, sizeof(new_context.span_id) - 1);
      new_context.span_id[sizeof(new_context.span_id) - 1] = '\0';

      new_context.parent_span_id[0] = '\0';

      set_trace_context(new_context);

      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, take_request_end, ros_request, new_context.trace_id, span_id_buf);
      #endif

      // Compute content hash of the REQUEST payload for cross-process
      // correlation. The client-send hashes the byte-identical request, so the
      // two hashes agree and the engine can match them (ROB-406).
      uint64_t content_hash = 0;
      uint32_t domain_id = get_dds_domain_id();

      // Emit service request TraceEvent
      TraceEvent event;
      event.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
      event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;

      event.trace_id[0] = '\0';        // Empty → agent correlates (ROB-406)
      event.parent_span_id[0] = '\0';  // Empty → agent correlates (ROB-406)

      std::memcpy(event.span_id, span_id_buf, sizeof(event.span_id) - 1);
      event.span_id[sizeof(event.span_id) - 1] = '\0';

      event.dds_domain_id = domain_id;
      // Content-hash correlation (the v0.3.0 scheme), now applied to services.
      event.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;
      // This is the CONSUMER end of the RPC: it correlates against the client.
      event.direction = robotops_msgs__msg__TraceEvent__DIRECTION_CONSUMER;
      // publisher_gid intentionally left empty: services share no DDS GID, so
      // both client-send and service-take emit "" and the key matches on
      // (service_name, content_hash, timestamp_bucket).

      if (request_header != nullptr) {
        event.sequence_number = request_header->request_id.sequence_number;
        // source_timestamp_ns seeds the correlation timestamp bucket; use the
        // DDS-provided source timestamp so client and server bucket together.
        event.source_timestamp_ns =
          static_cast<int64_t>(request_header->source_timestamp);
      }

      // Get service metadata
      ServiceMetadata metadata;
      if (get_service_metadata(service, metadata)) {
        if (metadata.request_members != nullptr) {
          content_hash = rmw_robotops::compute_message_hash(
            ros_request, metadata.request_members);
        }
        event.content_hash = content_hash;
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

rmw_ret_t
rmw_send_response(
  const rmw_service_t * service,
  rmw_request_id_t * request_header,
  void * ros_response)
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

  if (underlying_rmw_send_response == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  // Get trace context (lazy init)
  TraceContext context;
  bool tracing_active = is_tracing_enabled();
  char span_id_buf[17] = {0};
  uint64_t start_timestamp_ns = 0;

  // STEP 1: Capture trace context and record send timestamp (BEFORE underlying send)
  // Reads the thread-local context written by rmw_take_request on the same thread.
  // Reusing context.span_id pairs this RESPONSE event with the earlier REQUEST event
  // so span_reconstructor can merge them into a single SERVER span.
  // Note: We do NOT inject into DDS metadata for services —
  // related_sample_identity is reserved for DDS-RPC request-response correlation.
  if (tracing_active) {
    try {
      context = get_or_mint_trace_context();
      std::memcpy(span_id_buf, context.span_id, sizeof(span_id_buf));
      start_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

      #ifdef ROS_TRACING_ENABLED
      tracepoint(
        robotops, send_response_start,
        service, context.trace_id, span_id_buf);
      #endif
    } catch (...) {
      record_trace_failure();
      tracing_active = false;
    }
  }

  // STEP 2: REAL RESPONSE FIRST (Safety guarantee)
  rmw_ret_t ret = underlying_rmw_send_response(service, request_header, ros_response);

  // STEP 3: Emit SERVICE_RESPONSE event (paired with SERVICE_REQUEST by shared span_id)
  if (ret == RMW_RET_OK && tracing_active) {
    try {
      #ifdef ROS_TRACING_ENABLED
      tracepoint(robotops, send_response_end, service, true);
      #endif

      // Emit service response TraceEvent
      TraceEvent event;
      event.timestamp_ns = start_timestamp_ns;
      event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_RESPONSE;

      std::memcpy(event.trace_id, context.trace_id, sizeof(event.trace_id) - 1);
      event.trace_id[sizeof(event.trace_id) - 1] = '\0';

      std::memcpy(event.span_id, span_id_buf, sizeof(event.span_id) - 1);
      event.span_id[sizeof(event.span_id) - 1] = '\0';

      std::memcpy(event.parent_span_id, context.parent_span_id,
          sizeof(event.parent_span_id) - 1);
      event.parent_span_id[sizeof(event.parent_span_id) - 1] = '\0';

      event.dds_domain_id = get_dds_domain_id();
      // ROB-406: content-hash correlation. This RESPONSE is the PRODUCER end of
      // the response leg — the client's rmw_take_response (CONSUMER) correlates
      // against it by the response payload's content_hash.
      event.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;
      event.direction = robotops_msgs__msg__TraceEvent__DIRECTION_PRODUCER;

      if (request_header != nullptr) {
        event.sequence_number = request_header->sequence_number;
      }

      // Get service metadata
      ServiceMetadata metadata;
      if (get_service_metadata(service, metadata)) {
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

      // Emit CONTEXT_EXITED — the service handler is done processing (ROB-179)
      {
        rmw_robotops::TraceContextChangeEvent ctx_event;
        ctx_event.timestamp_ns = event.timestamp_ns;
        ctx_event.thread_id = rmw_robotops::get_current_thread_id();
        ctx_event.change_type = rmw_robotops::CONTEXT_CHANGE_EXITED;
        std::memcpy(ctx_event.node_name, event.node_name,
          sizeof(ctx_event.node_name) - 1);
        ctx_event.node_name[sizeof(ctx_event.node_name) - 1] = '\0';
        std::memcpy(ctx_event.node_namespace, event.node_namespace,
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
}  // extern "C"
