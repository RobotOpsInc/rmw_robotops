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

#include "rmw_robotops/utils.hpp"

#include <rcutils/logging_macros.h>

#include <cstring>

#include "robotops_msgs/msg/trace_event.h"

// xxHash library for fast content hashing
// We'll use a simple inline implementation to avoid external dependency for now
// TODO(ROB-55): Consider using official xxhash library for optimal performance

namespace rmw_robotops
{

/// Simple 64-bit hash (FNV-1a variant) for content hashing
/// This is a fallback implementation until we integrate xxhash library
static uint64_t fnv1a_hash_64(const void * data, size_t size) noexcept
{
  if (!data || size == 0) {
    return 0;
  }

  const uint8_t * bytes = static_cast<const uint8_t *>(data);
  uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
  constexpr uint64_t prime = 1099511628211ULL;  // FNV prime

  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= prime;
  }

  return hash;
}

uint64_t compute_content_hash(const void * data, size_t size) noexcept
{
  try {
    return fnv1a_hash_64(data, size);
  } catch (...) {
    RCUTILS_LOG_ERROR_NAMED("rmw_robotops", "Exception in compute_content_hash");
    return 0;
  }
}

bool is_intra_process_enabled(const void * publisher) noexcept
{
  // Conservative check: assume cross-process unless we can verify intra-process
  // TODO(ROB-55): Implement proper intra-process detection by checking
  // rmw_fastrtps_cpp internal structures or publisher options

  (void)publisher;  // Unused for now

  // For safety, always inject DDS metadata (works for both intra and cross-process)
  // Intra-process will use TLS context, cross-process will use DDS metadata
  return false;
}

/// Helper: Check if string ends with suffix
static bool ends_with(const char * str, const char * suffix) noexcept
{
  if (!str || !suffix) {
    return false;
  }

  size_t str_len = std::strlen(str);
  size_t suffix_len = std::strlen(suffix);

  if (suffix_len > str_len) {
    return false;
  }

  return std::strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

uint8_t detect_action_event_type(
  const char * topic_name,
  bool is_publisher,
  bool is_start_event) noexcept
{
  if (!topic_name) {
    // Default to regular pub/sub events
    if (is_publisher) {
      if (is_start_event) {
        return robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START;
      } else {
        return robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_END;
      }
    } else {
      if (is_start_event) {
        return robotops_msgs__msg__TraceEvent__EVENT_TAKE_RMW_START;
      } else {
        return robotops_msgs__msg__TraceEvent__EVENT_TAKE_RMW_END;
      }
    }
  }

  // Detect action topics by naming convention
  // ROS2 actions use: /_action/send_goal, /_action/cancel_goal, etc.
  if (ends_with(topic_name, "/_action/send_goal")) {
    return robotops_msgs__msg__TraceEvent__EVENT_ACTION_GOAL;
  }
  if (ends_with(topic_name, "/_action/cancel_goal")) {
    return robotops_msgs__msg__TraceEvent__EVENT_ACTION_CANCEL;
  }
  if (ends_with(topic_name, "/_action/get_result")) {
    return robotops_msgs__msg__TraceEvent__EVENT_ACTION_RESULT;
  }
  if (ends_with(topic_name, "/_action/feedback")) {
    return robotops_msgs__msg__TraceEvent__EVENT_ACTION_FEEDBACK;
  }

  // Not an action - use regular pub/sub event types
  if (is_publisher) {
    if (is_start_event) {
      return robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START;
    } else {
      return robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_END;
    }
  } else {
    if (is_start_event) {
      return robotops_msgs__msg__TraceEvent__EVENT_TAKE_RMW_START;
    } else {
      return robotops_msgs__msg__TraceEvent__EVENT_TAKE_RMW_END;
    }
  }
}

}  // namespace rmw_robotops
