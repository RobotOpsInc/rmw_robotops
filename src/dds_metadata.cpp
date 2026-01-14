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

#include <cstring>

#include "rmw_robotops/dds_metadata.hpp"
#include "rmw_robotops/trace_context.hpp"

namespace rmw_robotops
{

bool inject_trace_context_to_dds(
  void * dds_sample_info,
  const TraceContext & context) noexcept
{
  // DDS-agnostic design: No injection into DDS metadata
  //
  // rmw_robotops uses a passive observation approach that maintains DDS independence:
  //
  // INTRA-PROCESS: Context propagates via thread-local storage (TLS)
  //   - Publish sets TLS context
  //   - Subscribe on same thread reads TLS context
  //   - Works for same-process callback chains
  //
  // CROSS-PROCESS: Post-hoc correlation via metadata
  //   - Publisher emits: GID + timestamp + content_hash
  //   - Subscriber emits: GID + timestamp + content_hash
  //   - robot_agent correlates events after the fact
  //   - No DDS-specific features required
  //
  // This approach:
  //   ✓ Works with any DDS implementation (FastDDS, CycloneDDS, Connext)
  //   ✓ Never blocks real messages (Safety Guarantee #3)
  //   ✓ Zero changes to message payload or DDS wire format
  //   ✓ Passive observation only

  (void)dds_sample_info;  // Not used - we don't modify DDS metadata

  if (context.is_empty()) {
    return false;  // No context to propagate
  }

  // Context already in TLS from publish interception
  // Will be available for intra-process subscribers on same thread
  return true;
}

bool extract_trace_context_from_dds(
  const void * dds_sample_info,
  TraceContext & context) noexcept
{
  // DDS-agnostic design: No extraction from DDS metadata
  //
  // rmw_robotops uses a passive observation approach that maintains DDS independence:
  //
  // INTRA-PROCESS: Context retrieved from thread-local storage (TLS)
  //   - If context exists in TLS, it came from an upstream publish on same thread
  //   - Enables trace propagation through same-process callback chains
  //
  // CROSS-PROCESS: No context extraction (intentional)
  //   - Cross-process messages have no trace context in DDS
  //   - Instead, we emit correlation metadata (GID, timestamp, content_hash)
  //   - robot_agent correlates publish/subscribe events post-hoc
  //   - New trace_id minted for cross-process root spans
  //
  // When context.is_empty() after this call:
  //   - Expected for cross-process messages (by design)
  //   - Expected for first message in a trace (root span)
  //   - Expected for messages from non-rmw_robotops publishers

  (void)dds_sample_info;  // Not used - we don't extract from DDS metadata

  // Attempt to get context from thread-local storage (intra-process only)
  context = get_trace_context();

  if (context.is_empty()) {
    // No context available - caller will mint new trace_id (root span)
    return false;
  }

  // Context found in TLS - this is an intra-process continuation
  return true;
}

}  // namespace rmw_robotops
