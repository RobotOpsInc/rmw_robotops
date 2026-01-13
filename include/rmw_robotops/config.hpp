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

#ifndef RMW_ROBOTOPS__CONFIG_HPP_
#define RMW_ROBOTOPS__CONFIG_HPP_

#include <atomic>
#include <cstdint>

#include <robotops/config/v1/config.pb.h>  // NOLINT(build/include_order)

namespace rmw_robotops
{

/// Runtime tracing state (separate from immutable configuration)
/// This tracks dynamic state that changes during execution
struct TracingState
{
  /// Is tracing currently enabled? (Safety guarantee #6: runtime kill switch)
  /// Can be set to false at runtime for zero-overhead passthrough
  std::atomic<bool> enabled;

  /// Current consecutive failure count
  std::atomic<uint32_t> consecutive_failures;

  /// Auto-disable threshold (Safety guarantee #7)
  /// If consecutive failures exceed this, disable tracing
  uint32_t failure_threshold;
};

/// Get the global configuration (immutable, loaded from robotops-config + env vars)
/// Returns the generated protobuf config with environment variable overrides applied
const robotops::config::v1::Config & get_config() noexcept;

/// Get the global runtime tracing state (mutable)
/// This is separate from configuration and tracks dynamic execution state
TracingState & get_tracing_state() noexcept;

/// Check if tracing is currently enabled
/// This checks both the state flag and runtime failure threshold
inline bool is_tracing_enabled() noexcept
{
  TracingState & state = get_tracing_state();

  if (!state.enabled.load(std::memory_order_relaxed)) {
    return false;
  }

  // Auto-disable if failures exceed threshold (Safety guarantee #7)
  if (state.consecutive_failures.load(std::memory_order_relaxed) >= state.failure_threshold) {
    state.enabled.store(false, std::memory_order_relaxed);
    return false;
  }

  return true;
}

/// Record a tracing failure (increments consecutive failure count)
void record_trace_failure() noexcept;

/// Record a tracing success (resets consecutive failure count)
void record_trace_success() noexcept;

/// Get the underlying RMW implementation name
inline const char * get_underlying_rmw() noexcept
{
  return get_config().tracing().underlying_rmw().c_str();
}

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__CONFIG_HPP_
