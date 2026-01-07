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

#include <cstring>

#include "rmw_robotops/config.hpp"
#include "robotops_msgs/msg/trace_event.h"

// FastDDS headers for SampleInfo access (conditional)
#include "fastdds/dds/subscriber/SampleInfo.hpp"
#include "fastdds/rtps/common/SampleIdentity.h"

namespace rmw_robotops
{

/// FastDDS correlation strategy - extracts metadata from SampleInfo
///
/// This strategy leverages FastDDS's SampleInfo to extract:
/// - Publisher GID (writer GUID)
/// - Sequence number
/// - Source timestamp
/// - related_sample_identity (when available and not used by DDS-RPC)
///
/// Future enhancement: Direct related_sample_identity injection requires
/// accessing the underlying FastDDS DataWriter, which needs unwrapping
/// rmw_fastrtps_cpp internal structures. For now, we extract what's available
/// for post-processing correlation by robot_agent.
class FastDDSCorrelationStrategy : public CorrelationStrategy
{
public:
  FastDDSCorrelationStrategy() = default;

  bool inject_context(
    const rmw_publisher_t * publisher,
    const TraceContext & context,
    const void * serialized_msg,
    size_t msg_size) noexcept override
  {
    // TODO(ROB-55): Implement direct related_sample_identity injection
    //
    // To inject trace context into FastDDS WriteParams.related_sample_identity:
    // 1. Cast publisher->data to rmw_fastrtps_cpp's internal publisher type
    // 2. Get the underlying FastDDS DataWriter
    // 3. Encode context into SampleIdentity (writer_guid = trace_id, sequence = span_id hash)
    // 4. Set WriteParams.related_sample_identity() before write
    //
    // This requires intimate knowledge of rmw_fastrtps_cpp internals and
    // may break with RMW version changes. For now, we rely on robot_agent
    // post-processing correlation using extracted metadata.

    (void)publisher;
    (void)context;
    (void)serialized_msg;
    (void)msg_size;

    // Context is propagated via TLS (works for intra-process)
    // Cross-process correlation uses extracted GID/sequence/timestamp/hash
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

    try {
      if (dds_sample_info != nullptr) {
        auto * sample_info =
          static_cast<const eprosima::fastdds::dds::SampleInfo *>(dds_sample_info);

        // Extract publisher GID from SampleInfo
        const auto & guid = sample_info->sample_identity.writer_guid();
        metadata.publisher_gid = guid_to_hex_string(guid);

        // Extract sequence number
        metadata.sequence_number = sample_info->sample_identity.sequence_number().to64long();

        // Extract source timestamp (nanoseconds since epoch)
        metadata.source_timestamp_ns =
          sample_info->source_timestamp.to_ns();

        // Check related_sample_identity for injected trace context
        // (only valid for regular topics, not services/actions which use it for DDS-RPC)
        if (sample_info->related_sample_identity.writer_guid() !=
          eprosima::fastrtps::rtps::GUID_t::unknown())
        {
          // TODO(ROB-55): Decode trace context from related_sample_identity
          // For now, we don't have injection implemented, so this won't be populated
          RCUTILS_LOG_DEBUG_NAMED(
            "rmw_robotops",
            "related_sample_identity present but decoding not yet implemented");
        }

        metadata.correlation_method =
          robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE;
        metadata.content_hash = 0;  // Computed separately if needed

        // Note: Trace context not extracted from DDS (injection not yet implemented)
        // Context comes from TLS for intra-process, or is minted as new trace root
        context = TraceContext();  // Empty context (will mint new trace)
        return false;  // No context extracted (robot_agent will correlate)
      }

      // Fallback if FastDDS sample info not available
      (void)dds_sample_info;
      (void)message_info;
      (void)serialized_msg;
      (void)msg_size;

      metadata.publisher_gid = "";
      metadata.sequence_number = 0;
      metadata.source_timestamp_ns = 0;
      metadata.content_hash = 0;
      metadata.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_TIMESTAMP;

      context = TraceContext();
      return false;
    } catch (...) {
      RCUTILS_LOG_ERROR_NAMED("rmw_robotops", "Exception in extract_context");
      return false;
    }
  }

  bool is_deterministic() const noexcept override
  {
    // Current implementation uses fallback correlation (GID + sequence + timestamp)
    // TODO(ROB-55): Return true when related_sample_identity injection is implemented
    return false;
  }

  uint8_t get_correlation_method() const noexcept override
  {
    return robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE;
  }

  const char * get_name() const noexcept override
  {
    return "FastDDS-Enhanced";
  }

private:
  /// Convert GUID to hex string for correlation
  std::string guid_to_hex_string(const eprosima::fastrtps::rtps::GUID_t & guid) const
  {
    char hex[37];  // 16 bytes * 2 + null terminator
    snprintf(
      hex, sizeof(hex),
      "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
      guid.guidPrefix.value[0], guid.guidPrefix.value[1],
      guid.guidPrefix.value[2], guid.guidPrefix.value[3],
      guid.guidPrefix.value[4], guid.guidPrefix.value[5],
      guid.guidPrefix.value[6], guid.guidPrefix.value[7],
      guid.guidPrefix.value[8], guid.guidPrefix.value[9],
      guid.guidPrefix.value[10], guid.guidPrefix.value[11],
      guid.entityId.value[0], guid.entityId.value[1],
      guid.entityId.value[2], guid.entityId.value[3]);
    return std::string(hex);
  }
};

/// Fallback correlation strategy - uses GID + timestamp + content hash
///
/// This strategy works with any DDS implementation by extracting
/// available metadata from rmw_message_info_t and computing a
/// content hash of the serialized message.
class FallbackCorrelationStrategy : public CorrelationStrategy
{
public:
  FallbackCorrelationStrategy() = default;

  bool inject_context(
    const rmw_publisher_t * publisher,
    const TraceContext & context,
    const void * serialized_msg,
    size_t msg_size) noexcept override
  {
    // No DDS injection possible in fallback mode
    // Context propagation relies on TLS and post-processing correlation

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
        metadata.sequence_number = 0;  // Not available in fallback mode
      } else {
        metadata.publisher_gid = "";
        metadata.source_timestamp_ns = 0;
        metadata.sequence_number = 0;
      }

      // Content hash computed separately (expensive, done only when needed)
      metadata.content_hash = 0;
      metadata.correlation_method =
        robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_TIMESTAMP;

      // No trace context extracted from DDS
      context = TraceContext();
      return false;
    } catch (...) {
      RCUTILS_LOG_ERROR_NAMED("rmw_robotops", "Exception in fallback extract_context");
      return false;
    }
  }

  bool is_deterministic() const noexcept override
  {
    return false;  // Probabilistic correlation
  }

  uint8_t get_correlation_method() const noexcept override
  {
    return robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_TIMESTAMP;
  }

  const char * get_name() const noexcept override
  {
    return "Fallback-Timestamp";
  }

private:
  /// Convert GID to hex string
  std::string gid_to_hex_string(const uint8_t gid[24]) const noexcept
  {
    char hex[49];  // 24 bytes * 2 + null terminator
    for (size_t i = 0; i < 24; ++i) {
      snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", gid[i]);
    }
    return std::string(hex);
  }
};

// Factory function
std::unique_ptr<CorrelationStrategy> create_correlation_strategy()
{
  const char * underlying_rmw = get_underlying_rmw();

  // Detect FastDDS
  if (std::strstr(underlying_rmw, "fastrtps") != nullptr ||
    std::strstr(underlying_rmw, "fastdds") != nullptr)
  {
    RCUTILS_LOG_INFO_NAMED(
      "rmw_robotops",
      "Using FastDDS correlation strategy (enhanced metadata extraction)");
    return std::make_unique<FastDDSCorrelationStrategy>();
  }

  // Fallback for other DDS implementations
  RCUTILS_LOG_INFO_NAMED(
    "rmw_robotops",
    "Using fallback correlation strategy for %s",
    underlying_rmw);
  return std::make_unique<FallbackCorrelationStrategy>();
}

}  // namespace rmw_robotops
