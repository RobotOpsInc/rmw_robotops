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
#include "rosidl_runtime_c/string.h"
#include "rosidl_runtime_c/primitives_sequence.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

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

/// Hash a single field using introspection
static uint64_t hash_field(
  const uint8_t * message_ptr,
  const rosidl_typesupport_introspection_c__MessageMember * member,
  uint64_t hash) noexcept;

/// Forward declaration for recursive message hashing
static uint64_t hash_message_members(
  const uint8_t * message_ptr,
  const rosidl_typesupport_introspection_c__MessageMembers * members,
  uint64_t hash) noexcept;

/// Hash message content using introspection - walks entire structure
static uint64_t hash_message_members(
  const uint8_t * message_ptr,
  const rosidl_typesupport_introspection_c__MessageMembers * members,
  uint64_t hash) noexcept
{
  if (members == nullptr || message_ptr == nullptr) {
    return hash;
  }

  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto * member = &members->members_[i];
    hash = hash_field(message_ptr, member, hash);
  }

  return hash;
}

/// Hash a single field value
static uint64_t hash_field(
  const uint8_t * message_ptr,
  const rosidl_typesupport_introspection_c__MessageMember * member,
  uint64_t hash) noexcept
{
  const uint8_t * field_ptr = message_ptr + member->offset_;
  constexpr uint64_t prime = 1099511628211ULL;

  // Handle arrays and sequences
  if (member->is_array_) {
    size_t array_size = 0;
    const void * array_data = nullptr;

    if (member->array_size_ > 0 && !member->is_upper_bound_) {
      // Fixed-size array
      array_size = member->array_size_;
      array_data = field_ptr;
    } else {
      // Dynamic sequence - need to get size
      if (member->size_function != nullptr) {
        array_size = member->size_function(field_ptr);
      }
    }

    // Hash array elements
    for (size_t j = 0; j < array_size; ++j) {
      const void * element = nullptr;
      if (member->get_const_function != nullptr) {
        element = member->get_const_function(field_ptr, j);
      } else if (array_data != nullptr) {
        // Fixed array - calculate offset based on type
        size_t element_size = 0;
        switch (member->type_id_) {
          case rosidl_typesupport_introspection_c__ROS_TYPE_BOOL:
          case rosidl_typesupport_introspection_c__ROS_TYPE_BYTE:
          case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
          case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
            element_size = 1;
            break;
          case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
          case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
            element_size = 2;
            break;
          case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
          case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
          case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
            element_size = 4;
            break;
          case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
          case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
          case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
            element_size = 8;
            break;
          default:
            continue;
        }
        element = static_cast<const uint8_t *>(array_data) + (j * element_size);
      }

      if (element == nullptr) {
        continue;
      }

      // Hash based on element type
      switch (member->type_id_) {
        case rosidl_typesupport_introspection_c__ROS_TYPE_BOOL:
        case rosidl_typesupport_introspection_c__ROS_TYPE_BYTE:
        case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
        case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
        case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
        case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
        case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
        case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
        case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
        case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
        case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
        case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE: {
            size_t size = 0;
            switch (member->type_id_) {
              case rosidl_typesupport_introspection_c__ROS_TYPE_BOOL:
              case rosidl_typesupport_introspection_c__ROS_TYPE_BYTE:
              case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
              case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
                size = 1; break;
              case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
              case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
                size = 2; break;
              case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
              case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
              case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
                size = 4; break;
              case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
              case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
              case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
                size = 8; break;
              default: break;
            }
            const uint8_t * bytes = static_cast<const uint8_t *>(element);
            for (size_t k = 0; k < size; ++k) {
              hash ^= bytes[k];
              hash *= prime;
            }
            break;
          }
        case rosidl_typesupport_introspection_c__ROS_TYPE_STRING: {
            const auto * str = static_cast<const rosidl_runtime_c__String *>(element);
            if (str->data != nullptr) {
              for (size_t k = 0; k < str->size; ++k) {
                hash ^= static_cast<uint8_t>(str->data[k]);
                hash *= prime;
              }
            }
            break;
          }
        case rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE: {
            if (member->members_ != nullptr && member->members_->data != nullptr) {
              const auto * nested_members =
                static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(
                member->members_->data);
              hash = hash_message_members(
              static_cast<const uint8_t *>(element), nested_members, hash);
            }
            break;
          }
        default:
          break;
      }
    }
  } else {
    // Scalar field
    switch (member->type_id_) {
      case rosidl_typesupport_introspection_c__ROS_TYPE_BOOL:
      case rosidl_typesupport_introspection_c__ROS_TYPE_BYTE:
      case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
      case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
      case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
      case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
      case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
      case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
      case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
      case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
      case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
      case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE: {
          size_t size = 0;
          switch (member->type_id_) {
            case rosidl_typesupport_introspection_c__ROS_TYPE_BOOL:
            case rosidl_typesupport_introspection_c__ROS_TYPE_BYTE:
            case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
            case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
              size = 1; break;
            case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
            case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
              size = 2; break;
            case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
            case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
            case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
              size = 4; break;
            case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
            case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
            case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
              size = 8; break;
            default: break;
          }
          for (size_t k = 0; k < size; ++k) {
            hash ^= field_ptr[k];
            hash *= prime;
          }
          break;
        }
      case rosidl_typesupport_introspection_c__ROS_TYPE_STRING: {
          const auto * str = reinterpret_cast<const rosidl_runtime_c__String *>(field_ptr);
          if (str->data != nullptr) {
            for (size_t k = 0; k < str->size; ++k) {
              hash ^= static_cast<uint8_t>(str->data[k]);
              hash *= prime;
            }
          }
          break;
        }
      case rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE: {
          if (member->members_ != nullptr && member->members_->data != nullptr) {
            const auto * nested_members =
              static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(
              member->members_->data);
            hash = hash_message_members(field_ptr, nested_members, hash);
          }
          break;
        }
      default:
        break;
    }
  }

  return hash;
}

uint64_t compute_message_hash(
  const void * ros_message,
  const rosidl_typesupport_introspection_c__MessageMembers * members) noexcept
{
  try {
    if (ros_message == nullptr || members == nullptr) {
      return 0;
    }

    const uint64_t initial_hash = 14695981039346656037ULL;  // FNV offset basis
    return hash_message_members(
      static_cast<const uint8_t *>(ros_message),
      members,
      initial_hash);
  } catch (...) {
    RCUTILS_LOG_ERROR_NAMED("rmw_robotops", "Exception in compute_message_hash");
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
