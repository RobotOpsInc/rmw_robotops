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

#include "rmw_robotops/dds_metadata.hpp"
#include "rmw_robotops/trace_context.hpp"

#include <cstring>

namespace rmw_robotops
{

bool inject_trace_context_to_dds(
  void * dds_sample_info,
  const TraceContext & context) noexcept
{
  // TODO(ROB-55): Implement DDS property list injection for cross-process propagation
  //
  // Current limitation: This is a placeholder that stores context in thread-local storage.
  // It works for intra-process communication but does NOT propagate across processes.
  //
  // For cross-process propagation, we need to:
  // 1. Access FastDDS DataWriter WriteParams to set inline QoS or properties
  // 2. Serialize trace context into DDS user data or property list
  // 3. This requires deeper integration with the underlying RMW layer
  //
  // Alternative approaches to consider:
  // - Separate metadata topic (/robotops/trace_context) correlated by timestamp/sequence
  // - Extend ROS2 message types with optional trace metadata fields
  // - Wait for ROS2 to support custom per-message metadata
  //
  // For now, context is already in TLS from publish interception, so we just validate.

  (void)dds_sample_info;  // Unused in current implementation

  if (context.is_empty()) {
    return false;  // No context to inject
  }

  // Context already in thread-local storage from publish call
  // This will be extracted on the same thread during subscribe
  return true;
}

bool extract_trace_context_from_dds(
  const void * dds_sample_info,
  TraceContext & context) noexcept
{
  // TODO(ROB-55): Implement DDS property list extraction for cross-process propagation
  //
  // Current limitation: This is a placeholder that retrieves context from thread-local storage.
  // It works for intra-process communication but does NOT extract across processes.
  //
  // For cross-process propagation, we need to:
  // 1. Access FastDDS DataReader SampleInfo to get inline QoS or properties
  // 2. Deserialize trace context from DDS user data or property list
  // 3. This requires deeper integration with the underlying RMW layer
  //
  // For now, attempt to get context from thread-local storage (works intra-process only).

  (void)dds_sample_info;  // Unused in current implementation

  // Try to get existing context from thread-local storage
  context = get_trace_context();

  if (context.is_empty()) {
    // No context in TLS - this is expected for:
    // 1. Cross-process messages (not yet supported)
    // 2. First message in a trace (root span)
    // 3. Messages from nodes not using rmw_robotops
    return false;
  }

  return true;
}

}  // namespace rmw_robotops
