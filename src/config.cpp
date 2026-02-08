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
#include <fstream>
#include <optional>

#include <rcutils/logging_macros.h>  // NOLINT(build/include_order)
#include <robotops/config/v1/defaults.hpp>  // NOLINT(build/include_order)
#include <yaml-cpp/yaml.h>  // NOLINT(build/include_order)

#include "rmw_robotops/diagnostics_metrics.hpp"

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

// Helper to safely load YAML file
static std::optional<YAML::Node> load_yaml_file(const char * path) noexcept
{
  try {
    std::ifstream file(path);
    if (!file.is_open()) {
      return std::nullopt;
    }
    return YAML::Load(file);
  } catch (const std::exception & e) {
    RCUTILS_LOG_WARN_NAMED(
      "rmw_robotops",
      "Failed to parse YAML config at %s: %s",
      path, e.what());
    return std::nullopt;
  }
}

// Helper to apply YAML configuration to protobuf config
static void apply_yaml_config(
  robotops::config::v1::Config & config,
  const YAML::Node & yaml) noexcept
{
  try {
    // Schema version
    if (yaml["schema_version"]) {
      config.set_schema_version(yaml["schema_version"].as<std::string>());
    }

    // Tracing section
    if (yaml["tracing"]) {
      const auto & tracing = yaml["tracing"];

      if (tracing["enabled"]) {
        config.mutable_tracing()->set_enabled(tracing["enabled"].as<bool>());
      }

      if (tracing["underlying_rmw"]) {
        config.mutable_tracing()->set_underlying_rmw(
          tracing["underlying_rmw"].as<std::string>());
      }

      // Performance section
      if (tracing["performance"]) {
        const auto & perf = tracing["performance"];

        if (perf["failure_threshold"]) {
          config.mutable_tracing()->mutable_performance()->set_failure_threshold(
            perf["failure_threshold"].as<uint32_t>());
        }
      }

      // Diagnostics section
      if (tracing["diagnostics"]) {
        const auto & diag = tracing["diagnostics"];

        if (diag["enabled"]) {
          config.mutable_tracing()->mutable_diagnostics()->set_enabled(
            diag["enabled"].as<bool>());
        }

        if (diag["interval_secs"]) {
          config.mutable_tracing()->mutable_diagnostics()->set_interval_secs(
            diag["interval_secs"].as<int32_t>());
        }
      }
    }
  } catch (const std::exception & e) {
    RCUTILS_LOG_WARN_NAMED(
      "rmw_robotops",
      "Error applying YAML config values: %s",
      e.what());
  }
}

// Helper to apply environment variable overrides
static void apply_env_overrides(robotops::config::v1::Config & config) noexcept
{
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

  // ROBOTOPS_DIAGNOSTICS_ENABLED: Enable/disable diagnostics publishing
  bool diag_enabled = get_env_bool(
    "ROBOTOPS_DIAGNOSTICS_ENABLED",
    config.tracing().diagnostics().enabled());
  config.mutable_tracing()->mutable_diagnostics()->set_enabled(diag_enabled);

  // ROBOTOPS_DIAGNOSTICS_INTERVAL_SECS: Diagnostics publishing interval
  uint32_t diag_interval = get_env_uint32(
    "ROBOTOPS_DIAGNOSTICS_INTERVAL_SECS",
    config.tracing().diagnostics().interval_secs());
  config.mutable_tracing()->mutable_diagnostics()->set_interval_secs(
    static_cast<int32_t>(diag_interval));
}

// Load configuration with layered precedence:
// 0. robotops-config package defaults
// 1. /etc/robotops/config.yaml (system config)
// 2. ROBOTOPS_CONFIG_PATH env var (custom path)
// 3. Environment variable overrides (highest priority)
static robotops::config::v1::Config load_config() noexcept
{
  try {
    // Layer 0: Start with package defaults
    auto config = robotops::config::v1::CreateDefaultConfig();

    // Layer 1: Apply system config if exists
    constexpr const char * SYSTEM_CONFIG_PATH = "/etc/robotops/config.yaml";
    auto system_yaml = load_yaml_file(SYSTEM_CONFIG_PATH);
    if (system_yaml) {
      RCUTILS_LOG_INFO_NAMED(
        "rmw_robotops",
        "Loading system config from %s",
        SYSTEM_CONFIG_PATH);
      apply_yaml_config(config, *system_yaml);
    } else {
      RCUTILS_LOG_INFO_NAMED(
        "rmw_robotops",
        "No system config found at %s (using defaults)",
        SYSTEM_CONFIG_PATH);
    }

    // Layer 2: Apply custom config path if specified
    const char * custom_path = std::getenv("ROBOTOPS_CONFIG_PATH");
    if (custom_path && custom_path[0] != '\0') {
      auto custom_yaml = load_yaml_file(custom_path);
      if (custom_yaml) {
        RCUTILS_LOG_INFO_NAMED(
          "rmw_robotops",
          "Loading custom config from ROBOTOPS_CONFIG_PATH=%s",
          custom_path);
        apply_yaml_config(config, *custom_yaml);
      } else {
        RCUTILS_LOG_WARN_NAMED(
          "rmw_robotops",
          "ROBOTOPS_CONFIG_PATH=%s specified but file not found or invalid",
          custom_path);
      }
    }

    // Layer 3: Apply environment variable overrides
    apply_env_overrides(config);

    // Log final configuration
    RCUTILS_LOG_INFO_NAMED(
      "rmw_robotops",
      "Configuration loaded: schema_version=%s, tracing=%s, "
      "underlying_rmw=%s, failure_threshold=%u",
      config.schema_version().c_str(),
      config.tracing().enabled() ? "enabled" : "disabled",
      config.tracing().underlying_rmw().c_str(),
      config.tracing().performance().failure_threshold());

    return config;
  } catch (const std::exception & e) {
    // Fallback to hardcoded defaults if configuration loading fails
    RCUTILS_LOG_ERROR_NAMED(
      "rmw_robotops",
      "Failed to load configuration: %s. Using hardcoded fallbacks.",
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
  static const robotops::config::v1::Config config = load_config();
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
    mark_auto_disabled();  // Track for diagnostics

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
