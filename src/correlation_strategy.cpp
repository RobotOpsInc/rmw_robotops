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

#include "rmw_robotops/correlation_strategy.hpp"

#include <rcutils/logging_macros.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "robotops_msgs/msg/trace_event.h"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/utils.hpp"

namespace rmw_robotops
{

/// DDS-Agnostic correlation strategy
///
/// Uses metadata available from any DDS implementation:
/// - Publisher GID (24-byte unique identifier)
/// - Source timestamp (nanosecond precision)
/// - Content hash (computed via introspection)
///
/// robot_agent correlates publish/subscribe events post-hoc using this metadata.
class ContentHashCorrelationStrategy : public CorrelationStrategy
{
public:
  ContentHashCorrelationStrategy() = default;

  bool inject_context(
    const rmw_publisher_t * publisher,
    const TraceContext & context,
    const void * serialized_msg,
    size_t msg_size) noexcept override
  {
    // No DDS injection - context propagates via TLS (intra-process)
    // Cross-process correlation handled by robot_agent using metadata

    (void)publisher;
    (void)context;
    (void)serialized_msg;
    (void)msg_size;

    return true;
  }

  bool extract_context(
    const rmw_subscription_t * subscription,
    const rmw_message_info_t * message_info,
    const void * dds_sample_info,
    const void * serialized_msg,
    size_t msg_size,
    TraceContext & context,
    CorrelationMetadata & metadata) noexcept override
  {
    (void)subscription;
    (void)dds_sample_info;
    (void)serialized_msg;
    (void)msg_size;

    try {
      // Extract publisher GID from RMW message info
      if (message_info != nullptr) {
        metadata.publisher_gid = gid_to_hex_string(message_info->publisher_gid.data);
        metadata.source_timestamp_ns = message_info->source_timestamp;
        metadata.sequence_number = 0;  // Not used for content hash correlation
      } else {
        metadata.publisher_gid = "";
        metadata.source_timestamp_ns = 0;
        metadata.sequence_number = 0;
      }

      // Content hash computed separately in rmw_publisher/subscription using
      // compute_message_hash() - see rmw_publisher.cpp and rmw_subscription.cpp
      metadata.content_hash = 0;
      metadata.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;

      // No trace context extracted from DDS
      // robot_agent correlates using: GID + timestamp + content_hash
      context = TraceContext();
      return false;
    } catch (...) {
      RCUTILS_LOG_ERROR_NAMED("rmw_robotops", "Exception in extract_context");
      return false;
    }
  }

  bool is_deterministic() const noexcept override
  {
    return false;  // Probabilistic correlation (robot_agent post-processes)
  }

  uint8_t get_correlation_method() const noexcept override
  {
    return robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;
  }

  const char * get_name() const noexcept override
  {
    return "ContentHash-DDS-Agnostic";
  }

private:
  /// Convert GID to hex string for correlation
  std::string gid_to_hex_string(const uint8_t gid[24]) const noexcept
  {
    char hex[49];  // 24 bytes * 2 + null terminator
    for (size_t i = 0; i < 24; ++i) {
      snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", gid[i]);
    }
    return std::string(hex);
  }
};

// Factory function - always returns DDS-agnostic strategy
std::unique_ptr<CorrelationStrategy> create_correlation_strategy()
{
  RCUTILS_LOG_INFO_NAMED(
    "rmw_robotops",
    "Using DDS-agnostic content hash correlation (works with any DDS)");
  return std::make_unique<ContentHashCorrelationStrategy>();
}

}  // namespace rmw_robotops
