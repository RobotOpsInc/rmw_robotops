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
#include <memory>

// Include robotops-config generated types
#include <robotops/config/v1/config.pb.h>

namespace rmw_robotops
{

/// Runtime configuration for rmw_robotops
/// Wraps the robotops-config generated Config protobuf with runtime state
class Config
{
public:
  /// Constructor - loads from generated defaults and applies environment variable overrides
  Config() noexcept;

  /// Get the underlying protobuf config (immutable defaults)
  const robotops::config::v1::Config & proto_config() const noexcept
  {
    return *proto_config_;
  }

  /// Is tracing enabled? (Safety guarantee #6: runtime kill switch)
  /// Can be set to false at runtime for zero-overhead passthrough
  std::atomic<bool> tracing_enabled;

  /// Underlying RMW implementation to delegate to
  /// Example: "rmw_fastrtps_cpp", "rmw_cyclonedds_cpp"
  std::string underlying_rmw;

  /// Auto-disable threshold (Safety guarantee #7)
  /// If consecutive failures exceed this, disable tracing
  uint32_t failure_threshold;

  /// Current consecutive failure count
  std::atomic<uint32_t> consecutive_failures;

private:
  /// Protobuf config with defaults from robotops-config
  std::unique_ptr<robotops::config::v1::Config> proto_config_;
};

/// Get the global configuration singleton
/// Thread-safe lazy initialization
Config & get_config() noexcept;

/// Check if tracing is enabled
/// Fast atomic read (relaxed memory order for performance)
inline bool is_tracing_enabled() noexcept
{
  return get_config().tracing_enabled.load(std::memory_order_relaxed);
}

/// Enable tracing (runtime control)
inline void enable_tracing() noexcept
{
  get_config().tracing_enabled.store(true, std::memory_order_relaxed);
}

/// Disable tracing (runtime control)
inline void disable_tracing() noexcept
{
  get_config().tracing_enabled.store(false, std::memory_order_relaxed);
}

/// Record a trace emission failure
/// Auto-disables tracing if failures exceed threshold (Safety guarantee #7)
void record_trace_failure() noexcept;

/// Record a successful trace emission (resets failure counter)
void record_trace_success() noexcept;

/// Get the underlying RMW implementation name
inline const char * get_underlying_rmw() noexcept
{
  return get_config().underlying_rmw.c_str();
}

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__CONFIG_HPP_
