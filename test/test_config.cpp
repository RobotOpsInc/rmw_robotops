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

#include <gtest/gtest.h>

#include <cstdlib>
#include <thread>
#include <vector>

#include "rmw_robotops/config.hpp"

using rmw_robotops::get_config;
using rmw_robotops::get_tracing_state;
using rmw_robotops::get_underlying_rmw;
using rmw_robotops::is_tracing_enabled;
using rmw_robotops::record_trace_failure;
using rmw_robotops::record_trace_success;

class ConfigTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    // Save original environment
    saved_enabled_ = std::getenv("ROBOTOPS_TRACING_ENABLED");
    saved_rmw_ = std::getenv("ROBOTOPS_UNDERLYING_RMW");
    saved_threshold_ = std::getenv("ROBOTOPS_FAILURE_THRESHOLD");
  }

  void TearDown() override
  {
    // Restore original environment
    if (saved_enabled_) {
      setenv("ROBOTOPS_TRACING_ENABLED", saved_enabled_, 1);
    } else {
      unsetenv("ROBOTOPS_TRACING_ENABLED");
    }

    if (saved_rmw_) {
      setenv("ROBOTOPS_UNDERLYING_RMW", saved_rmw_, 1);
    } else {
      unsetenv("ROBOTOPS_UNDERLYING_RMW");
    }

    if (saved_threshold_) {
      setenv("ROBOTOPS_FAILURE_THRESHOLD", saved_threshold_, 1);
    } else {
      unsetenv("ROBOTOPS_FAILURE_THRESHOLD");
    }
  }

  const char * saved_enabled_;
  const char * saved_rmw_;
  const char * saved_threshold_;
};

TEST_F(ConfigTest, DefaultConfiguration) {
  const auto & config = get_config();

  // Should have tracing configuration
  EXPECT_TRUE(config.has_tracing());

  // Should have default underlying RMW (non-empty string)
  EXPECT_FALSE(config.tracing().underlying_rmw().empty());

  // Should have performance config with failure threshold
  EXPECT_TRUE(config.tracing().has_performance());
  EXPECT_GT(config.tracing().performance().failure_threshold(), 0u);

  // Tracing state should start with zero failures
  auto & state = get_tracing_state();
  EXPECT_EQ(0u, state.consecutive_failures.load());
}

TEST_F(ConfigTest, TracingEnabledByDefault) {
  // Default should be enabled (unless overridden by env var)
  const auto & config = get_config();
  EXPECT_EQ(config.tracing().enabled(), is_tracing_enabled());
}

TEST_F(ConfigTest, TracingStateCanBeToggled) {
  auto & state = get_tracing_state();

  // Enable
  state.enabled.store(true);
  EXPECT_TRUE(is_tracing_enabled());

  // Disable
  state.enabled.store(false);
  EXPECT_FALSE(is_tracing_enabled());

  // Re-enable
  state.enabled.store(true);
  EXPECT_TRUE(is_tracing_enabled());
}

TEST_F(ConfigTest, RecordFailureSuccess) {
  auto & state = get_tracing_state();

  // Reset state
  state.consecutive_failures.store(0);
  state.enabled.store(true);

  // Record a success (should keep failures at 0)
  record_trace_success();
  EXPECT_EQ(0u, state.consecutive_failures.load());

  // Record a failure
  record_trace_failure();
  EXPECT_EQ(1u, state.consecutive_failures.load());

  // Record another failure
  record_trace_failure();
  EXPECT_EQ(2u, state.consecutive_failures.load());

  // Record success (should reset)
  record_trace_success();
  EXPECT_EQ(0u, state.consecutive_failures.load());
}

TEST_F(ConfigTest, AutoDisableOnFailures) {
  auto & state = get_tracing_state();

  // Set a low threshold for testing
  state.failure_threshold = 5;
  state.consecutive_failures.store(0);
  state.enabled.store(true);

  EXPECT_TRUE(is_tracing_enabled());

  // Record failures up to threshold
  for (uint32_t i = 0; i < state.failure_threshold; ++i) {
    record_trace_failure();
    EXPECT_TRUE(is_tracing_enabled()) << "Should still be enabled at failure " << i;
  }

  // One more failure should auto-disable
  record_trace_failure();
  EXPECT_FALSE(is_tracing_enabled()) << "Should auto-disable after exceeding threshold";
}

TEST_F(ConfigTest, UnderlyingRMW) {
  const char * rmw = get_underlying_rmw();
  EXPECT_NE(nullptr, rmw);
  // Should be a valid string (non-empty)
  EXPECT_GT(std::strlen(rmw), 0u);
}

TEST_F(ConfigTest, ConfigIsImmutable) {
  const auto & config1 = get_config();
  const auto & config2 = get_config();

  // Should return the same instance
  EXPECT_EQ(&config1, &config2);

  // Schema version should be set
  EXPECT_FALSE(config1.schema_version().empty());
}

TEST_F(ConfigTest, ThreadSafety) {
  // Test that atomic operations are thread-safe
  constexpr size_t NUM_THREADS = 10;
  constexpr size_t OPS_PER_THREAD = 1000;

  auto & state = get_tracing_state();
  std::vector<std::thread> threads;

  for (size_t t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&]() {
        for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
        // Toggle tracing
          if (i % 2 == 0) {
            state.enabled.store(true);
          } else {
            state.enabled.store(false);
          }

        // Check state (should never crash)
          (void)is_tracing_enabled();
        }
    });
  }

  for (auto & thread : threads) {
    thread.join();
  }

  // Should complete without crashes
  SUCCEED();
}
