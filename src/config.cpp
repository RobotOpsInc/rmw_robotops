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

#include <cstdlib>
#include <cstring>

#include <rcutils/logging_macros.h>  // NOLINT(build/include_order)
#include <robotops/config/v1/defaults.hpp>  // NOLINT(build/include_order)

namespace rmw_robotops
{

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

// Load configuration from robotops-config defaults + environment variable overrides
static robotops::config::v1::Config load_config_with_env_overrides() noexcept
{
  try {
    // Start with generated defaults
    auto config = robotops::config::v1::CreateDefaultConfig();

    // Apply environment variable overrides
    // ROBOTOPS_TRACING_ENABLED: Enable/disable tracing
    bool enabled = get_env_bool(
      "ROBOTOPS_TRACING_ENABLED",
      config.tracing().enabled());
    config.mutable_tracing()->set_enabled(enabled);

    // ROBOTOPS_UNDERLYING_RMW: Which RMW to delegate to
    const char * underlying_rmw = get_env_or_default(
      "ROBOTOPS_UNDERLYING_RMW",
      config.tracing().underlying_rmw().c_str());
    config.mutable_tracing()->set_underlying_rmw(underlying_rmw);

    // ROBOTOPS_FAILURE_THRESHOLD: Auto-disable threshold
    uint32_t failure_threshold = get_env_uint32(
      "ROBOTOPS_FAILURE_THRESHOLD",
      config.tracing().performance().failure_threshold());
    config.mutable_tracing()->mutable_performance()->set_failure_threshold(failure_threshold);

    // Log configuration
    RCUTILS_LOG_INFO_NAMED(
      "rmw_robotops",
      "Configuration loaded: schema_version=%s, tracing=%s, "
      "underlying_rmw=%s, failure_threshold=%u",
      config.schema_version().c_str(),
      enabled ? "enabled" : "disabled",
      underlying_rmw,
      failure_threshold);

    return config;
  } catch (const std::exception & e) {
    // Fallback to hardcoded defaults if robotops-config fails
    RCUTILS_LOG_ERROR_NAMED(
      "rmw_robotops",
      "Failed to load robotops-config defaults: %s. Using hardcoded fallbacks.",
      e.what());

    robotops::config::v1::Config config;
    config.set_schema_version("1.0.0");
    config.mutable_tracing()->set_enabled(true);
    config.mutable_tracing()->set_underlying_rmw("rmw_fastrtps_cpp");
    config.mutable_tracing()->mutable_performance()->set_failure_threshold(100);
    return config;
  }
}

const robotops::config::v1::Config & get_config() noexcept
{
  static const robotops::config::v1::Config config = load_config_with_env_overrides();
  return config;
}

TracingState & get_tracing_state() noexcept
{
  static TracingState state{
    get_config().tracing().enabled(),  // enabled
    0,  // consecutive_failures
    // failure_threshold
    static_cast<uint32_t>(get_config().tracing().performance().failure_threshold())
  };
  return state;
}

void record_trace_failure() noexcept
{
  TracingState & state = get_tracing_state();

  uint32_t failures = state.consecutive_failures.fetch_add(1, std::memory_order_relaxed) + 1;

  if (failures > state.failure_threshold) {
    // Auto-disable tracing (safety guarantee #7)
    state.enabled.store(false, std::memory_order_relaxed);

    RCUTILS_LOG_ERROR_NAMED(
      "rmw_robotops",
      "Tracing auto-disabled after %u consecutive failures (threshold: %u)",
      failures, state.failure_threshold);
  }
}

void record_trace_success() noexcept
{
  TracingState & state = get_tracing_state();
  state.consecutive_failures.store(0, std::memory_order_relaxed);
}

}  // namespace rmw_robotops
