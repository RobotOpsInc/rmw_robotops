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

#ifndef RMW_ROBOTOPS__SPAN_ID_GENERATOR_HPP_
#define RMW_ROBOTOPS__SPAN_ID_GENERATOR_HPP_

#include <cstdint>

namespace rmw_robotops
{

/// Generate a 128-bit trace ID as a 32-character hex string
/// Output buffer must be at least 33 bytes (32 chars + null terminator)
/// Thread-safe via thread-local PRNG state
void generate_trace_id(char * out_buffer) noexcept;

/// Generate a 64-bit span ID as a 16-character hex string
/// Output buffer must be at least 17 bytes (16 chars + null terminator)
/// Thread-safe via thread-local PRNG state
void generate_span_id(char * out_buffer) noexcept;

/// Initialize the PRNG seed for this thread
/// Called automatically on first use, but can be called explicitly
/// Uses high-resolution timer + thread ID for uniqueness
void init_span_id_generator() noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__SPAN_ID_GENERATOR_HPP_
