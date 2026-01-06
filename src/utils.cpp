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

}  // namespace rmw_robotops
