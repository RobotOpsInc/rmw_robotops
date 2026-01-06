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

#ifndef RMW_ROBOTOPS__CORRELATION_STRATEGY_HPP_
#define RMW_ROBOTOPS__CORRELATION_STRATEGY_HPP_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "rmw/rmw.h"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/visibility_control.hpp"
#include "robotops_msgs/msg/trace_event.h"

namespace rmw_robotops
{

/// Correlation metadata extracted from a received message
struct CorrelationMetadata
{
  std::string publisher_gid;        ///< DDS publisher GUID (hex string)
  uint64_t sequence_number;         ///< Message sequence number
  int64_t source_timestamp_ns;      ///< Publisher's timestamp
  uint64_t content_hash;            ///< xxHash64 of message content (0 if not computed)
  uint8_t correlation_method;       ///< Method used (from TraceEvent constants)
};

/// Abstract base class for cross-process trace context correlation strategies
///
/// Different DDS implementations have different capabilities for propagating
/// metadata with messages. This abstraction allows runtime selection of the
/// best available strategy.
class CorrelationStrategy
{
public:
  virtual ~CorrelationStrategy() = default;

  /// Inject trace context into DDS metadata before publishing
  ///
  /// @param publisher RMW publisher handle
  /// @param context Trace context to inject
  /// @param serialized_msg Serialized message data (for content hashing if needed)
  /// @param msg_size Size of serialized message in bytes
  /// @return true if injection succeeded, false otherwise
  ///
  /// @note This is called BEFORE the underlying RMW publish call
  /// @note Must be noexcept for safety (failures are logged, not propagated)
  virtual bool inject_context(
    const rmw_publisher_t * publisher,
    const TraceContext & context,
    const void * serialized_msg,
    size_t msg_size) noexcept = 0;

  /// Extract trace context and correlation metadata from DDS metadata
  ///
  /// @param subscription RMW subscription handle
  /// @param message_info RMW message info structure
  /// @param dds_sample_info DDS-specific sample info (cast to appropriate type)
  /// @param serialized_msg Serialized message data (for content hashing if needed)
  /// @param msg_size Size of serialized message in bytes
  /// @param[out] context Extracted trace context (may be empty if none found)
  /// @param[out] metadata Correlation metadata for robot_agent processing
  /// @return true if extraction succeeded, false if no context found
  ///
  /// @note This is called AFTER the underlying RMW take call
  /// @note Must be noexcept for safety (failures are logged, not propagated)
  virtual bool extract_context(
    const rmw_subscription_t * subscription,
    const rmw_message_info_t * message_info,
    const void * dds_sample_info,
    const void * serialized_msg,
    size_t msg_size,
    TraceContext & context,
    CorrelationMetadata & metadata) noexcept = 0;

  /// Check if this strategy provides deterministic correlation
  ///
  /// @return true if correlation is deterministic (e.g., FastDDS related_sample_identity)
  ///         false if probabilistic (e.g., fallback hash-based correlation)
  ///
  /// Deterministic correlation means the subscriber can definitively match
  /// a received message to the exact publisher that sent it.
  ///
  /// Probabilistic correlation means robot_agent must do post-processing
  /// to correlate pub->sub based on timing, content hash, etc.
  virtual bool is_deterministic() const noexcept = 0;

  /// Get the correlation method enum value for TraceEvent messages
  ///
  /// @return Correlation method constant (from robotops_msgs/msg/TraceEvent)
  virtual uint8_t get_correlation_method() const noexcept = 0;

  /// Get human-readable strategy name for diagnostics
  ///
  /// @return Strategy name (e.g., "FastDDS-related_sample_identity", "Fallback-Hash")
  virtual const char * get_name() const noexcept = 0;
};

/// Create the appropriate correlation strategy for the current DDS implementation
///
/// Factory function that detects the underlying RMW/DDS at runtime and returns
/// the best available strategy.
///
/// @return Unique pointer to correlation strategy (never null)
///
/// @note If FastDDS with related_sample_identity support is detected, returns
///       FastDDSCorrelationStrategy. Otherwise returns FallbackCorrelationStrategy.
RMW_ROBOTOPS_PUBLIC
std::unique_ptr<CorrelationStrategy> create_correlation_strategy();

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__CORRELATION_STRATEGY_HPP_
