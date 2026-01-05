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

#include "rmw_robotops/config.hpp"

#include <rcutils/logging_macros.h>

#include <cstdlib>
#include <cstring>

namespace rmw_robotops
{

// Default failure threshold (safety guarantee #7)
constexpr uint32_t DEFAULT_FAILURE_THRESHOLD = 100;

// Helper to get environment variable with default
static const char * get_env_or_default(const char * name, const char * default_value) noexcept
{
  const char * value = std::getenv(name);
  return value ? value : default_value;
}

// Helper to parse boolean from environment variable
static bool get_env_bool(const char * name, bool default_value) noexcept
{
  const char * value = std::getenv(name);
  if (!value) {
    return default_value;
  }

  // Accept: "1", "true", "TRUE", "yes", "YES"
  if (std::strcmp(value, "1") == 0 ||
    std::strcmp(value, "true") == 0 ||
    std::strcmp(value, "TRUE") == 0 ||
    std::strcmp(value, "yes") == 0 ||
    std::strcmp(value, "YES") == 0)
  {
    return true;
  }

  // Accept: "0", "false", "FALSE", "no", "NO"
  if (std::strcmp(value, "0") == 0 ||
    std::strcmp(value, "false") == 0 ||
    std::strcmp(value, "FALSE") == 0 ||
    std::strcmp(value, "no") == 0 ||
    std::strcmp(value, "NO") == 0)
  {
    return false;
  }

  // Invalid value, use default
  RCUTILS_LOG_WARN_NAMED(
    "rmw_robotops",
    "Invalid boolean value for %s: '%s' (using default: %s)",
    name, value, default_value ? "true" : "false");
  return default_value;
}

// Helper to parse uint32 from environment variable
static uint32_t get_env_uint32(const char * name, uint32_t default_value) noexcept
{
  const char * value = std::getenv(name);
  if (!value) {
    return default_value;
  }

  char * endptr = nullptr;
  uint64_t parsed = std::strtoull(value, &endptr, 10);

  if (endptr == value || *endptr != '\0' || parsed > UINT32_MAX) {
    RCUTILS_LOG_WARN_NAMED(
      "rmw_robotops",
      "Invalid uint32 value for %s: '%s' (using default: %u)",
      name, value, default_value);
    return default_value;
  }

  return static_cast<uint32_t>(parsed);
}

Config::Config() noexcept
: tracing_enabled(true),
  underlying_rmw(nullptr),
  topic_filter_regex(nullptr),
  failure_threshold(DEFAULT_FAILURE_THRESHOLD),
  consecutive_failures(0)
{
  // Load configuration from environment variables

  // ROBOTOPS_TRACING_ENABLED: Enable/disable tracing (default: true)
  bool enabled = get_env_bool("ROBOTOPS_TRACING_ENABLED", true);
  tracing_enabled.store(enabled, std::memory_order_relaxed);

  // ROBOTOPS_UNDERLYING_RMW: Which RMW to delegate to (required!)
  underlying_rmw = get_env_or_default("ROBOTOPS_UNDERLYING_RMW", "rmw_fastrtps_cpp");

  // ROBOTOPS_TRACE_TOPIC_FILTER: Optional topic filter regex
  const char * filter = std::getenv("ROBOTOPS_TRACE_TOPIC_FILTER");
  if (filter && filter[0] != '\0') {
    topic_filter_regex = filter;
  }

  // ROBOTOPS_FAILURE_THRESHOLD: Auto-disable threshold (default: 100)
  failure_threshold = get_env_uint32("ROBOTOPS_FAILURE_THRESHOLD", DEFAULT_FAILURE_THRESHOLD);

  // Log configuration
  RCUTILS_LOG_INFO_NAMED(
    "rmw_robotops",
    "Configuration loaded: tracing=%s, underlying_rmw=%s, failure_threshold=%u",
    enabled ? "enabled" : "disabled",
    underlying_rmw,
    failure_threshold);

  if (topic_filter_regex) {
    RCUTILS_LOG_INFO_NAMED(
      "rmw_robotops",
      "Topic filter enabled: %s",
      topic_filter_regex);
  }
}

// Global configuration singleton
static Config g_config;

Config & get_config() noexcept
{
  return g_config;
}

void record_trace_failure() noexcept
{
  Config & config = get_config();

  uint32_t failures = config.consecutive_failures.fetch_add(1, std::memory_order_relaxed) + 1;

  if (failures > config.failure_threshold) {
    // Auto-disable tracing (safety guarantee #7)
    config.tracing_enabled.store(false, std::memory_order_relaxed);

    RCUTILS_LOG_ERROR_NAMED(
      "rmw_robotops",
      "Tracing auto-disabled after %u consecutive failures (threshold: %u)",
      failures, config.failure_threshold);
  }
}

void record_trace_success() noexcept
{
  Config & config = get_config();
  config.consecutive_failures.store(0, std::memory_order_relaxed);
}

}  // namespace rmw_robotops
