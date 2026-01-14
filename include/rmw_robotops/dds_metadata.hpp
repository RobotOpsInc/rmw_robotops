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

#ifndef RMW_ROBOTOPS__DDS_METADATA_HPP_
#define RMW_ROBOTOPS__DDS_METADATA_HPP_

#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/visibility_control.hpp"

namespace rmw_robotops
{

/// Propagate trace context (intra-process only via thread-local storage)
///
/// @param dds_sample_info DDS sample info pointer (unused - DDS-agnostic design)
/// @param context Trace context to propagate
/// @return true if context is valid, false otherwise
///
/// @note DDS-agnostic design: No injection into DDS metadata
///
/// **Design rationale:**
/// - INTRA-PROCESS: Context propagates via thread-local storage (TLS)
/// - CROSS-PROCESS: Post-hoc correlation using GID + timestamp + content_hash
/// - Works identically with any DDS implementation (FastDDS, CycloneDDS, Connext)
/// - Never modifies DDS wire format or message payload (passive observation)
/// - See src/dds_metadata.cpp for full implementation details
RMW_ROBOTOPS_PUBLIC
bool inject_trace_context_to_dds(
  void * dds_sample_info,
  const TraceContext & context) noexcept;

/// Retrieve trace context (intra-process only via thread-local storage)
///
/// @param dds_sample_info DDS sample info pointer (unused - DDS-agnostic design)
/// @param[out] context Retrieved trace context (empty if not found)
/// @return true if context found in TLS, false otherwise
///
/// @note DDS-agnostic design: No extraction from DDS metadata
///
/// **Design rationale:**
/// - INTRA-PROCESS: Retrieves context from thread-local storage if available
/// - CROSS-PROCESS: Returns false (no context in DDS by design)
/// - When false returned, caller mints new trace_id for root span
/// - Cross-process correlation handled by robot_agent using emitted metadata
/// - See src/dds_metadata.cpp for full implementation details
RMW_ROBOTOPS_PUBLIC
bool extract_trace_context_from_dds(
  const void * dds_sample_info,
  TraceContext & context) noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__DDS_METADATA_HPP_
