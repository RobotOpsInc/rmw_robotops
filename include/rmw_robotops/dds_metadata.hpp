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

/// Inject trace context into DDS metadata for cross-process propagation
///
/// @param dds_sample_info Pointer to DDS sample info (FastDDS SampleInfo or equivalent)
/// @param context Trace context to inject
/// @return true if injection succeeded, false otherwise
///
/// @note Current implementation is a placeholder using thread-local storage.
///       Does NOT propagate across processes. See implementation for TODO details.
///
/// **Multi-DDS Extension Point:**
/// The void* parameter is intentionally DDS-agnostic to support multiple DDS implementations.
/// Future implementations should:
/// - Cast void* to the appropriate DDS-specific type
///   (e.g., FastDDS WriteParams, CycloneDDS ddsi_serdata)
/// - Inject trace context using that DDS's metadata/property mechanism
/// - Select implementation at runtime based on ROBOTOPS_UNDERLYING_RMW environment variable
/// - Return false gracefully if the DDS implementation doesn't support metadata injection
RMW_ROBOTOPS_PUBLIC
bool inject_trace_context_to_dds(
  void * dds_sample_info,
  const TraceContext & context) noexcept;

/// Extract trace context from DDS metadata for cross-process propagation
///
/// @param dds_sample_info Pointer to DDS sample info (FastDDS SampleInfo or equivalent)
/// @param[out] context Extracted trace context
/// @return true if extraction succeeded, false if no context found
///
/// @note Current implementation is a placeholder using thread-local storage.
///       Does NOT extract across processes. See implementation for TODO details.
///
/// **Multi-DDS Extension Point:**
/// The void* parameter is intentionally DDS-agnostic to support multiple DDS implementations.
/// Future implementations should:
/// - Cast void* to the appropriate DDS-specific type
///   (e.g., FastDDS SampleInfo, CycloneDDS ddsi_serdata)
/// - Extract trace context using that DDS's metadata/property mechanism
/// - Select implementation at runtime based on ROBOTOPS_UNDERLYING_RMW environment variable
/// - Return false gracefully if no context is found or DDS doesn't support metadata extraction
RMW_ROBOTOPS_PUBLIC
bool extract_trace_context_from_dds(
  const void * dds_sample_info,
  TraceContext & context) noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__DDS_METADATA_HPP_
