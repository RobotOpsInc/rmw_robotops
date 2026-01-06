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

// LTTng tracepoint definitions for rmw_robotops
// These tracepoints enable correlation with ros2_tracing for hierarchical span reconstruction
//
// Note: LTTng tracepoint headers use a multi-read pattern that differs from traditional
// header guards. The TRACEPOINT_HEADER_MULTI_READ macro allows controlled re-inclusion.
// NOLINT(build/header_guard)

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER robotops

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "rmw_robotops/tp_call.h"

#if !defined(RMW_ROBOTOPS__TP_CALL_H_) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define RMW_ROBOTOPS__TP_CALL_H_

#include <lttng/tracepoint.h>

#include <stdint.h>
#include <stdbool.h>

// Publish RMW start event
// Emitted at the beginning of rmw_publish, before underlying RMW call
TRACEPOINT_EVENT(
  robotops,
  publish_rmw_start,
  TP_ARGS(
    const void *, msg_ptr,
    const char *, topic,
    const char *, trace_id,
    const char *, span_id,
    const char *, parent_span_id,
    uint64_t, content_hash
  ),
  TP_FIELDS(
    ctf_integer_hex(const void *, msg_ptr, msg_ptr)
    ctf_string(topic, topic)
    ctf_string(trace_id, trace_id)
    ctf_string(span_id, span_id)
    ctf_string(parent_span_id, parent_span_id)
    ctf_integer(uint64_t, content_hash, content_hash)
  )
)

// Publish RMW end event
// Emitted after rmw_publish completes
TRACEPOINT_EVENT(
  robotops,
  publish_rmw_end,
  TP_ARGS(
    const void *, msg_ptr,
    bool, success
  ),
  TP_FIELDS(
    ctf_integer_hex(const void *, msg_ptr, msg_ptr)
    ctf_integer(unsigned char, success, success ? 1 : 0)
  )
)

// Take RMW start event
// Emitted at the beginning of rmw_take, before underlying RMW call
TRACEPOINT_EVENT(
  robotops,
  take_rmw_start,
  TP_ARGS(
    const void *, subscription_ptr
  ),
  TP_FIELDS(
    ctf_integer_hex(const void *, subscription_ptr, subscription_ptr)
  )
)

// Take RMW end event
// Emitted after rmw_take completes successfully
TRACEPOINT_EVENT(
  robotops,
  take_rmw_end,
  TP_ARGS(
    const void *, msg_ptr,
    const char *, trace_id,
    const char *, span_id,
    const char *, publisher_gid,
    int64_t, source_timestamp_ns,
    uint64_t, content_hash
  ),
  TP_FIELDS(
    ctf_integer_hex(const void *, msg_ptr, msg_ptr)
    ctf_string(trace_id, trace_id)
    ctf_string(span_id, span_id)
    ctf_string(publisher_gid, publisher_gid)
    ctf_integer(int64_t, source_timestamp_ns, source_timestamp_ns)
    ctf_integer(uint64_t, content_hash, content_hash)
  )
)

#endif  // RMW_ROBOTOPS__TP_CALL_H_

#include <lttng/tracepoint-event.h>
