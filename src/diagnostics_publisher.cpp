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

#include <unistd.h>  // gethostname

#ifdef __linux__
#include <sys/syscall.h>  // SYS_gettid
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw_robotops/config.hpp"
#include "rmw_robotops/diagnostics_metrics.hpp"
#include "rmw_robotops/diagnostics_publisher.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "robotops_msgs/msg/diagnostics_report.h"
#include "rosidl_runtime_c/string_functions.h"

// Forward declarations of underlying RMW functions
extern "C" {
extern rmw_node_t * (* underlying_rmw_create_node)(
  rmw_context_t *, const char *, const char *);
extern rmw_ret_t (* underlying_rmw_destroy_node)(rmw_node_t *);
extern rmw_publisher_t * (* underlying_rmw_create_publisher)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_publisher_options_t *);
extern rmw_ret_t (* underlying_rmw_destroy_publisher)(
  rmw_node_t *, rmw_publisher_t *);
extern rmw_ret_t (* underlying_rmw_publish)(
  const rmw_publisher_t *, const void *, rmw_publisher_allocation_t *);
}

namespace rmw_robotops
{

namespace
{

// Background publisher state
std::atomic<bool> diagnostics_running{false};
std::thread diagnostics_thread;
rmw_node_t * diagnostics_node = nullptr;
rmw_publisher_t * diagnostics_publisher = nullptr;

// CPU tracking state
std::atomic<uint64_t> last_cpu_time{0};
std::atomic<int64_t> last_cpu_check_time{0};

// Cached node identification (set on first publish)
char cached_node_name[256] = "";
char cached_node_namespace[256] = "";
char cached_hostname[256] = "";
bool node_info_cached = false;

/// Clock sync information
struct ClockSyncInfo
{
  uint8_t method;              // CLOCK_SYNC_NONE, NTP, PTP, GPS
  int64_t estimated_skew_ns;
  uint64_t skew_uncertainty_ns;
  bool acceptable;
};

/// Parse chrony tracking output for skew and uncertainty
bool parse_chrony_tracking(ClockSyncInfo * info) noexcept
{
  try {
    FILE * fp = popen("chronyc tracking 2>/dev/null", "r");
    if (!fp) {return false;}

    char line[256];
    bool found_offset = false;
    double offset_seconds = 0.0;
    double rms_offset = 0.0;

    while (fgets(line, sizeof(line), fp) != nullptr) {
      // Look for "System time" line: "System time     : 0.000000123 seconds slow of NTP time"
      if (strstr(line, "System time") != nullptr) {
        char * seconds_str = strstr(line, "seconds");
        if (seconds_str) {
          // Parse backwards to find the number
          char * end = seconds_str - 1;
          while (end > line && isspace(*end)) {--end;}
          char * start = end;
          while (start > line && (isdigit(*start) || *start == '.' || *start == '-')) {
            --start;
          }
          offset_seconds = atof(start + 1);
          found_offset = true;
        }
      }
      // Look for "RMS offset" line for uncertainty
      if (strstr(line, "RMS offset") != nullptr) {
        char * seconds_str = strstr(line, "seconds");
        if (seconds_str) {
          char * end = seconds_str - 1;
          while (end > line && isspace(*end)) {--end;}
          char * start = end;
          while (start > line && (isdigit(*start) || *start == '.' || *start == '-')) {
            --start;
          }
          rms_offset = atof(start + 1);
        }
      }
    }

    pclose(fp);

    if (found_offset) {
      info->method = robotops_msgs__msg__DiagnosticsReport__CLOCK_SYNC_NTP;
      info->estimated_skew_ns = static_cast<int64_t>(offset_seconds * 1e9);
      info->skew_uncertainty_ns = static_cast<uint64_t>(rms_offset * 1e9);
      // Consider acceptable if skew < 1 second
      info->acceptable = std::abs(info->estimated_skew_ns) < 1000000000LL;
      return true;
    }
  } catch (...) {
    // Graceful degradation
  }
  return false;
}

/// Check systemd-timesyncd status
bool check_timesyncd(ClockSyncInfo * info) noexcept
{
  try {
    // Check if synchronized file exists
    FILE * fp = fopen("/run/systemd/timesync/synchronized", "r");
    if (fp) {
      fclose(fp);
      info->method = robotops_msgs__msg__DiagnosticsReport__CLOCK_SYNC_NTP;
      info->estimated_skew_ns = 0;  // timesyncd doesn't easily expose skew
      info->skew_uncertainty_ns = 100000000;  // ~100ms uncertainty estimate
      info->acceptable = true;
      return true;
    }
  } catch (...) {
    // Graceful degradation
  }
  return false;
}

/// Check PTP (ptp4l) status
bool check_ptp4l(ClockSyncInfo * info) noexcept
{
  try {
    FILE * fp = popen("pmc -u -b 0 'GET CURRENT_DATA_SET' 2>/dev/null", "r");
    if (!fp) {return false;}

    char line[256];
    bool found_offset = false;

    while (fgets(line, sizeof(line), fp) != nullptr) {
      // Look for offsetFromMaster
      if (strstr(line, "offsetFromMaster") != nullptr) {
        char * number = line;
        while (*number && !isdigit(*number) && *number != '-') {++number;}
        if (*number) {
          int64_t offset_ns = atoll(number);
          info->method = robotops_msgs__msg__DiagnosticsReport__CLOCK_SYNC_PTP;
          info->estimated_skew_ns = offset_ns;
          info->skew_uncertainty_ns = 1000;  // PTP typically <1us uncertainty
          info->acceptable = std::abs(offset_ns) < 1000000000LL;  // <1s
          found_offset = true;
          break;
        }
      }
    }

    pclose(fp);
    return found_offset;
  } catch (...) {
    // Graceful degradation
  }
  return false;
}

/// Detect clock synchronization method and status
/// Returns clock sync information or CLOCK_SYNC_NONE if detection fails
ClockSyncInfo detect_clock_sync() noexcept
{
  ClockSyncInfo info;
  info.method = robotops_msgs__msg__DiagnosticsReport__CLOCK_SYNC_NONE;
  info.estimated_skew_ns = 0;
  info.skew_uncertainty_ns = 0;
  info.acceptable = false;

  // Try in priority order (most precise first)
  if (check_ptp4l(&info)) {
    return info;
  }

  if (parse_chrony_tracking(&info)) {
    return info;
  }

  if (check_timesyncd(&info)) {
    return info;
  }

  // If no sync found, that's acceptable for non-time-critical systems
  info.acceptable = true;
  return info;
}

/// Compute memory usage by reading /proc/self/status
/// Returns RSS (Resident Set Size) in bytes
uint64_t compute_memory_usage() noexcept
{
  try {
    FILE * fp = fopen("/proc/self/status", "r");
    if (!fp) {
      // Fallback to estimated size if /proc not available
      const uint64_t queue_size = sizeof(TraceEvent) * DEFAULT_TRACE_QUEUE_SIZE;
      const uint64_t cache_estimate = 20 * 1024;
      return queue_size + cache_estimate + 1024;
    }

    char line[256];
    uint64_t rss_kb = 0;

    while (fgets(line, sizeof(line), fp) != nullptr) {
      if (strstr(line, "VmRSS:") == line) {
        // Format: "VmRSS:     12345 kB"
        char * number = line + 6;  // Skip "VmRSS:"
        while (*number && !isdigit(*number)) {++number;}
        rss_kb = strtoull(number, nullptr, 10);
        break;
      }
    }

    fclose(fp);
    return rss_kb * 1024;  // Convert KB to bytes
  } catch (...) {
    return 0;  // Graceful degradation
  }
}

/// Compute CPU usage percentage for the diagnostics thread
/// Returns CPU usage as percentage (0.0-100.0)
float compute_cpu_percent() noexcept
{
  try {
    #ifdef __linux__
    // Get the thread ID via syscall
    pid_t tid = syscall(SYS_gettid);

    char stat_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/self/task/%d/stat", tid);

    FILE * fp = fopen(stat_path, "r");
    if (!fp) {
      return 0.0f;
    }

    // Read stat file - format: pid (comm) state ... utime stime ...
    // We need fields 14 (utime) and 15 (stime) in clock ticks
    char line[1024];
    if (!fgets(line, sizeof(line), fp)) {
      fclose(fp);
      return 0.0f;
    }
    fclose(fp);

    // Parse the stat line - need to skip past the comm field which can contain spaces
    char * p = strchr(line, ')');
    if (!p) {
      return 0.0f;
    }
    p += 2;  // Skip ") "

    // Now parse remaining fields
    uint64_t utime = 0, stime = 0;
    int field = 3;  // We're at field 3 now (after pid, comm, state)
    char * saveptr = nullptr;
    char * token = strtok_r(p, " ", &saveptr);

    while (token && field < 15) {
      if (field == 13) {  // utime
        utime = strtoull(token, nullptr, 10);
      } else if (field == 14) {  // stime
        stime = strtoull(token, nullptr, 10);
      }
      token = strtok_r(nullptr, " ", &saveptr);
      ++field;
    }

    // Get current time in nanoseconds
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    // Calculate CPU time in nanoseconds (assume 100 ticks/sec on most Linux)
    const uint64_t ticks_per_sec = 100;  // sysconf(_SC_CLK_TCK) would be more accurate
    uint64_t cpu_time_ns = (utime + stime) * (1000000000ULL / ticks_per_sec);

    // Calculate delta since last check
    uint64_t prev_cpu_time = last_cpu_time.load(std::memory_order_relaxed);
    int64_t prev_check_time = last_cpu_check_time.load(std::memory_order_relaxed);

    if (prev_check_time == 0) {
      // First call - just store values
      last_cpu_time.store(cpu_time_ns, std::memory_order_relaxed);
      last_cpu_check_time.store(now_ns, std::memory_order_relaxed);
      return 0.0f;
    }

    // Calculate percentage: (delta_cpu / delta_wall) * 100
    int64_t delta_cpu = cpu_time_ns - prev_cpu_time;
    int64_t delta_wall = now_ns - prev_check_time;

    last_cpu_time.store(cpu_time_ns, std::memory_order_relaxed);
    last_cpu_check_time.store(now_ns, std::memory_order_relaxed);

    if (delta_wall <= 0) {
      return 0.0f;
    }

    float cpu_percent = (static_cast<float>(delta_cpu) / static_cast<float>(delta_wall)) * 100.0f;
    return std::min(cpu_percent, 100.0f);  // Cap at 100%

    #else
    // Non-Linux systems: return 0
    return 0.0f;
    #endif
  } catch (...) {
    return 0.0f;  // Graceful degradation
  }
}

/// Populate diagnostics report message
void populate_diagnostics_report(
  robotops_msgs__msg__DiagnosticsReport & msg,
  const ClockSyncInfo & clock_info) noexcept
{
  try {
    // Timestamp
    auto now = std::chrono::system_clock::now().time_since_epoch();
    msg.timestamp.sec = static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(now).count());
    msg.timestamp.nanosec = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() % 1000000000ULL);

    // Node identification (cached on first publish)
    if (!node_info_cached) {
      // Get hostname
      if (gethostname(cached_hostname, sizeof(cached_hostname)) != 0) {
        snprintf(cached_hostname, sizeof(cached_hostname), "unknown");
      }

      // Node name and namespace will be populated from first created node
      // For now, use a placeholder
      snprintf(cached_node_name, sizeof(cached_node_name), "rmw_robotops");
      snprintf(cached_node_namespace, sizeof(cached_node_namespace), "/");

      node_info_cached = true;
    }

    rosidl_runtime_c__String__assign(&msg.node_name, cached_node_name);
    rosidl_runtime_c__String__assign(&msg.node_namespace, cached_node_namespace);
    rosidl_runtime_c__String__assign(&msg.hostname, cached_hostname);

    // Clock synchronization status
    msg.clock_sync_method = clock_info.method;
    msg.estimated_skew_ns = clock_info.estimated_skew_ns;
    msg.skew_uncertainty_ns = clock_info.skew_uncertainty_ns;
    msg.clock_sync_acceptable = clock_info.acceptable;

    // Tracing health metrics
    DiagnosticsMetrics & metrics = get_diagnostics_metrics();
    msg.traces_emitted = metrics.traces_emitted.load(std::memory_order_relaxed);
    msg.traces_dropped = metrics.traces_dropped.load(std::memory_order_relaxed);

    // Correlation metrics (rmw_robotops doesn't do correlation - robot_agent does)
    msg.correlation_successes = 0;
    msg.correlation_failures = 0;
    msg.correlation_ambiguous = 0;

    // Queue utilization (0.0-1.0)
    const size_t queue_size = get_trace_event_queue().size();
    msg.queue_utilization = static_cast<float>(queue_size) /
      static_cast<float>(DEFAULT_TRACE_QUEUE_SIZE);

    msg.tracing_enabled = is_tracing_enabled();
    msg.auto_disabled = metrics.auto_disabled.load(std::memory_order_relaxed);

    // DDS capabilities
    rosidl_runtime_c__String__assign(&msg.rmw_implementation, get_underlying_rmw());
    msg.related_sample_identity_supported = false;  // We removed DDS injection
    rosidl_runtime_c__String__assign(&msg.correlation_strategy, "content_hash");

    // Resource usage
    msg.memory_used_bytes = compute_memory_usage();
    msg.cpu_percent = compute_cpu_percent();
  } catch (...) {
    // Safety guarantee: Never propagate exceptions
  }
}

/// Background thread function that publishes diagnostics
void diagnostics_thread_func() noexcept
{
  robotops_msgs__msg__DiagnosticsReport msg;
  robotops_msgs__msg__DiagnosticsReport__init(&msg);

  // Get publishing interval
  int32_t interval_secs = get_diagnostics_interval_secs();
  if (interval_secs <= 0) {
    interval_secs = 10;  // Default fallback
  }

  auto next_publish_time = std::chrono::steady_clock::now();
  auto next_clock_check_time = next_publish_time;

  // Cached clock sync info (expensive to compute, refresh less often)
  ClockSyncInfo cached_clock_info = detect_clock_sync();

  while (diagnostics_running.load(std::memory_order_relaxed)) {
    auto now = std::chrono::steady_clock::now();

    // Check clock sync less frequently (every 60 seconds)
    if (now >= next_clock_check_time) {
      cached_clock_info = detect_clock_sync();
      next_clock_check_time = now + std::chrono::seconds(60);
    }

    // Publish diagnostics at configured interval
    if (now >= next_publish_time) {
      // Populate message
      populate_diagnostics_report(msg, cached_clock_info);

      // Publish using underlying RMW (bypass interception)
      if (diagnostics_publisher != nullptr && underlying_rmw_publish != nullptr) {
        underlying_rmw_publish(diagnostics_publisher, &msg, nullptr);
      }

      // Clean up message strings for next iteration
      robotops_msgs__msg__DiagnosticsReport__fini(&msg);
      robotops_msgs__msg__DiagnosticsReport__init(&msg);

      next_publish_time = now + std::chrono::seconds(interval_secs);
    }

    // Sleep briefly to avoid spinning (100ms polling)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  robotops_msgs__msg__DiagnosticsReport__fini(&msg);
}

}  // anonymous namespace

rmw_ret_t start_diagnostics_publisher(rmw_context_t * context) noexcept
{
  // Check if already running
  if (diagnostics_running.load(std::memory_order_relaxed)) {
    return RMW_RET_OK;  // Idempotent
  }

  // Check if diagnostics publishing is enabled
  if (!is_diagnostics_enabled()) {
    return RMW_RET_OK;  // No-op if diagnostics disabled
  }

  // Create a node for the diagnostics publisher
  if (underlying_rmw_create_node == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  diagnostics_node = underlying_rmw_create_node(
    context, "rmw_robotops_diagnostics", "/robotops");
  if (diagnostics_node == nullptr) {
    RMW_SET_ERROR_MSG("Failed to create diagnostics publisher node");
    return RMW_RET_ERROR;
  }

  // Get type support for DiagnosticsReport message
  const rosidl_message_type_support_t * type_support =
    ROSIDL_GET_MSG_TYPE_SUPPORT(robotops_msgs, msg, DiagnosticsReport);
  if (type_support == nullptr) {
    underlying_rmw_destroy_node(diagnostics_node);
    diagnostics_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to get DiagnosticsReport type support");
    return RMW_RET_ERROR;
  }

  // Create publisher with best-effort QoS
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 10;  // Small queue for diagnostics

  rmw_publisher_options_t pub_options = rmw_get_default_publisher_options();

  if (underlying_rmw_create_publisher == nullptr) {
    underlying_rmw_destroy_node(diagnostics_node);
    diagnostics_node = nullptr;
    RMW_SET_ERROR_MSG("Underlying RMW create_publisher not available");
    return RMW_RET_ERROR;
  }

  diagnostics_publisher = underlying_rmw_create_publisher(
    diagnostics_node, type_support, "/robotops/diagnostics", &qos, &pub_options);

  if (diagnostics_publisher == nullptr) {
    underlying_rmw_destroy_node(diagnostics_node);
    diagnostics_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to create diagnostics publisher");
    return RMW_RET_ERROR;
  }

  // Start background thread
  diagnostics_running.store(true, std::memory_order_relaxed);
  try {
    diagnostics_thread = std::thread(diagnostics_thread_func);
  } catch (...) {
    diagnostics_running.store(false, std::memory_order_relaxed);
    underlying_rmw_destroy_publisher(diagnostics_node, diagnostics_publisher);
    underlying_rmw_destroy_node(diagnostics_node);
    diagnostics_publisher = nullptr;
    diagnostics_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to start diagnostics thread");
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

rmw_ret_t stop_diagnostics_publisher() noexcept
{
  // Check if running
  if (!diagnostics_running.load(std::memory_order_relaxed)) {
    return RMW_RET_OK;  // Idempotent
  }

  // Signal thread to stop
  diagnostics_running.store(false, std::memory_order_relaxed);

  // Wait for thread to finish
  if (diagnostics_thread.joinable()) {
    try {
      diagnostics_thread.join();
    } catch (...) {
      // Safety guarantee: Never propagate exceptions
    }
  }

  // Clean up publisher and node
  if (diagnostics_publisher != nullptr && underlying_rmw_destroy_publisher != nullptr) {
    underlying_rmw_destroy_publisher(diagnostics_node, diagnostics_publisher);
    diagnostics_publisher = nullptr;
  }

  if (diagnostics_node != nullptr && underlying_rmw_destroy_node != nullptr) {
    underlying_rmw_destroy_node(diagnostics_node);
    diagnostics_node = nullptr;
  }

  return RMW_RET_OK;
}

}  // namespace rmw_robotops
