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

#ifndef RMW_ROBOTOPS__UTILS_HPP_
#define RMW_ROBOTOPS__UTILS_HPP_

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

// Forward declaration for introspection type
struct rosidl_typesupport_introspection_c__MessageMembers_s;
typedef struct rosidl_typesupport_introspection_c__MessageMembers_s
  rosidl_typesupport_introspection_c__MessageMembers;

namespace rmw_robotops
{

/// Correlation metadata extracted from a received message
///
/// Used for post-hoc correlation by robot_agent to match
/// publish events with corresponding subscribe events.
struct CorrelationMetadata
{
  std::string publisher_gid;        ///< DDS publisher GUID (hex string)
  uint64_t sequence_number;         ///< Message sequence number
  int64_t source_timestamp_ns;      ///< Publisher's timestamp
  uint64_t content_hash;            ///< xxHash64 of message content (0 if not computed)
  uint8_t correlation_method;       ///< Method used (from TraceEvent constants)
};

/// Get the DDS domain ID from environment
/// @return Domain ID from ROS_DOMAIN_ID env var, or 0 if not set
inline uint32_t get_dds_domain_id() noexcept
{
  const char * domain_str = std::getenv("ROS_DOMAIN_ID");
  if (!domain_str) {
    return 0;
  }

  char * endptr = nullptr;
  int64_t domain = std::strtoll(domain_str, &endptr, 10);

  // Validate: entire string consumed and value in valid range (0-232)
  if (endptr == domain_str || *endptr != '\0' || domain < 0 || domain > 232) {
    return 0;
  }

  return static_cast<uint32_t>(domain);
}

/// Compute xxHash64 of message content for correlation
/// @param data Pointer to serialized message data
/// @param size Size of data in bytes
/// @return xxHash64 hash value, or 0 on failure
///
/// @note Uses XXH64 from xxhash library for fast hashing (~33ns per 1KB)
/// @note Returns 0 if data is null or size is 0 (indicates no hash computed)
uint64_t compute_content_hash(const void * data, size_t size) noexcept;

/// Compute content hash of ROS message using introspection
/// @param ros_message Pointer to typed ROS message
/// @param members Introspection metadata for the message type
/// @return FNV-1a hash of message content, or 0 on failure
///
/// @note Walks entire message structure, dereferencing pointers for strings/arrays
/// @note Same message content produces same hash on publisher and subscriber
/// @note Handles all ROS2 types: primitives, strings, arrays, nested messages
/// @note Returns 0 if ros_message or members is null
uint64_t compute_message_hash(
  const void * ros_message,
  const rosidl_typesupport_introspection_c__MessageMembers * members) noexcept;

/// Detect action event type from topic name
/// @param topic_name ROS topic name
/// @param is_publisher true for publish, false for subscribe
/// @return TraceEvent event_type constant
///
/// ROS2 actions use standard topic naming:
/// - /_action/send_goal - Goal request
/// - /_action/cancel_goal - Cancel request
/// - /_action/get_result - Result request
/// - /_action/feedback - Feedback messages
/// - /_action/status - Status updates
///
/// @note Returns EVENT_PUBLISH_RMW_START/END or EVENT_TAKE_RMW_START/END if not an action
uint8_t detect_action_event_type(
  const char * topic_name,
  bool is_publisher,
  bool is_start_event) noexcept;

/// Get the current thread ID as a uint64_t
/// Uses std::hash for portability — not guaranteed unique but consistent per thread
inline uint64_t get_current_thread_id() noexcept
{
  return static_cast<uint64_t>(
    std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

/// Convert DDS GID to hex string for correlation
/// @param gid DDS publisher GUID (24 bytes)
/// @return Hex string representation (48 characters)
inline std::string gid_to_hex_string(const uint8_t gid[24]) noexcept
{
  char hex[49];  // 24 bytes * 2 + null terminator
  for (size_t i = 0; i < 24; ++i) {
    snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", gid[i]);
  }
  return std::string(hex);
}

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__UTILS_HPP_
