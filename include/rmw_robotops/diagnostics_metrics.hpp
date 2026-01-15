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

#ifndef RMW_ROBOTOPS__DIAGNOSTICS_METRICS_HPP_
#define RMW_ROBOTOPS__DIAGNOSTICS_METRICS_HPP_

#include <atomic>
#include <cstdint>

namespace rmw_robotops
{

/// Diagnostics metrics collected during runtime
/// These counters are incremented from hot paths (publish/subscribe)
/// and read periodically by the diagnostics publisher thread
struct DiagnosticsMetrics
{
  /// Total number of trace events successfully emitted
  std::atomic<uint64_t> traces_emitted{0};

  /// Total number of trace events dropped (queue full)
  std::atomic<uint64_t> traces_dropped{0};

  /// True if tracing was automatically disabled due to failures
  std::atomic<bool> auto_disabled{false};
};

/// Get the global diagnostics metrics singleton
/// @return Reference to global metrics (thread-safe)
DiagnosticsMetrics & get_diagnostics_metrics() noexcept;

/// Increment traces_emitted counter (called from hot path)
/// @note noexcept - never throws, zero overhead
inline void increment_traces_emitted() noexcept
{
  get_diagnostics_metrics().traces_emitted.fetch_add(1, std::memory_order_relaxed);
}

/// Increment traces_dropped counter (called from hot path)
/// @note noexcept - never throws, zero overhead
inline void increment_traces_dropped() noexcept
{
  get_diagnostics_metrics().traces_dropped.fetch_add(1, std::memory_order_relaxed);
}

/// Mark tracing as auto-disabled (called when failure threshold exceeded)
/// @note noexcept - never throws
inline void mark_auto_disabled() noexcept
{
  get_diagnostics_metrics().auto_disabled.store(true, std::memory_order_relaxed);
}

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__DIAGNOSTICS_METRICS_HPP_
