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

namespace rmw_robotops
{

/// Get the DDS domain ID from environment
/// @return Domain ID from ROS_DOMAIN_ID env var, or 0 if not set
inline uint32_t get_dds_domain_id() noexcept
{
  const char * domain_str = std::getenv("ROS_DOMAIN_ID");
  if (!domain_str) {
    return 0;
  }

  char * endptr = nullptr;
  unsigned long domain = std::strtoul(domain_str, &endptr, 10);

  // Validate: entire string consumed and value in valid range (0-232)
  if (endptr == domain_str || *endptr != '\0' || domain > 232) {
    return 0;
  }

  return static_cast<uint32_t>(domain);
}

/// Compute xxHash64 of message content for fallback correlation
/// @param data Pointer to serialized message data
/// @param size Size of data in bytes
/// @return xxHash64 hash value, or 0 on failure
///
/// @note Uses XXH64 from xxhash library for fast hashing (~33ns per 1KB)
/// @note Returns 0 if data is null or size is 0 (indicates no hash computed)
uint64_t compute_content_hash(const void * data, size_t size) noexcept;

/// Check if intra-process communication is enabled for this publisher
/// @param publisher RMW publisher handle
/// @return true if intra-process is enabled, false otherwise
///
/// @note Intra-process messages don't need DDS metadata injection
/// @note This is a conservative check - returns false if unsure
bool is_intra_process_enabled(const void * publisher) noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__UTILS_HPP_
