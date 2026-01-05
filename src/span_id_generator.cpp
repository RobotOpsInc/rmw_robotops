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

#include "rmw_robotops/span_id_generator.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

namespace rmw_robotops
{

// Thread-local PRNG state using xorshift128+
// Fast, good distribution, thread-safe via TLS
struct XorShift128State
{
  uint64_t s[2];
  bool initialized;

  XorShift128State() : s{0, 0}, initialized(false) {}
};

thread_local XorShift128State tls_rng_state;

/// Fast xorshift128+ PRNG
/// Good statistical properties, very fast (no divisions)
static uint64_t xorshift128plus(XorShift128State & state) noexcept
{
  uint64_t s1 = state.s[0];
  const uint64_t s0 = state.s[1];
  state.s[0] = s0;
  s1 ^= s1 << 23;
  state.s[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
  return state.s[1] + s0;
}

void init_span_id_generator() noexcept
{
  if (tls_rng_state.initialized) {
    return;  // Already initialized
  }

  // Seed using high-resolution time + thread ID
  // This provides good uniqueness across threads and processes
  auto now = std::chrono::high_resolution_clock::now();
  auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
    now.time_since_epoch()).count();

  // Mix in thread ID
  auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());

  // Initialize state with mixed entropy
  tls_rng_state.s[0] = static_cast<uint64_t>(nanos);
  tls_rng_state.s[1] = static_cast<uint64_t>(thread_id) ^ (nanos >> 32);

  // Ensure we don't have all-zero state (xorshift requirement)
  if (tls_rng_state.s[0] == 0 && tls_rng_state.s[1] == 0) {
    tls_rng_state.s[0] = 1;
  }

  tls_rng_state.initialized = true;
}

/// Convert uint64 to 16-character hex string (lowercase)
static void uint64_to_hex(uint64_t value, char * out) noexcept
{
  constexpr char hex_chars[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i) {
    out[i] = hex_chars[value & 0xF];
    value >>= 4;
  }
  out[16] = '\0';
}

void generate_span_id(char * out_buffer) noexcept
{
  if (!tls_rng_state.initialized) {
    init_span_id_generator();
  }

  uint64_t id = xorshift128plus(tls_rng_state);
  uint64_to_hex(id, out_buffer);
}

void generate_trace_id(char * out_buffer) noexcept
{
  if (!tls_rng_state.initialized) {
    init_span_id_generator();
  }

  // Generate two 64-bit values for 128-bit trace ID
  uint64_t high = xorshift128plus(tls_rng_state);
  uint64_t low = xorshift128plus(tls_rng_state);

  // Convert to hex string (32 chars)
  uint64_to_hex(high, out_buffer);
  uint64_to_hex(low, out_buffer + 16);
}

}  // namespace rmw_robotops
