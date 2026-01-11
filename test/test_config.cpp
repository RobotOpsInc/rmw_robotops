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

#include "rmw_robotops/config.hpp"

using rmw_robotops::Config;
using rmw_robotops::disable_tracing;
using rmw_robotops::enable_tracing;
using rmw_robotops::get_config;
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
  Config & config = get_config();

  // Should have default underlying RMW (non-empty string)
  EXPECT_FALSE(config.underlying_rmw.empty());

  // Failure threshold should be positive
  EXPECT_GT(config.failure_threshold, 0u);

  // Should start with zero failures
  EXPECT_EQ(0u, config.consecutive_failures.load());
}

TEST_F(ConfigTest, TracingEnabledByDefault) {
  // Default should be enabled
  EXPECT_TRUE(is_tracing_enabled());
}

TEST_F(ConfigTest, EnableDisableTracing) {
  // Enable
  enable_tracing();
  EXPECT_TRUE(is_tracing_enabled());

  // Disable
  disable_tracing();
  EXPECT_FALSE(is_tracing_enabled());

  // Re-enable
  enable_tracing();
  EXPECT_TRUE(is_tracing_enabled());
}

TEST_F(ConfigTest, RecordFailureSuccess) {
  Config & config = get_config();

  // Reset state
  config.consecutive_failures.store(0);
  enable_tracing();

  // Record a success (should keep failures at 0)
  record_trace_success();
  EXPECT_EQ(0u, config.consecutive_failures.load());

  // Record a failure
  record_trace_failure();
  EXPECT_EQ(1u, config.consecutive_failures.load());

  // Record another failure
  record_trace_failure();
  EXPECT_EQ(2u, config.consecutive_failures.load());

  // Record success (should reset)
  record_trace_success();
  EXPECT_EQ(0u, config.consecutive_failures.load());
}

TEST_F(ConfigTest, AutoDisableOnFailures) {
  Config & config = get_config();

  // Set a low threshold for testing
  config.failure_threshold = 5;
  config.consecutive_failures.store(0);
  enable_tracing();

  EXPECT_TRUE(is_tracing_enabled());

  // Record failures up to threshold
  for (uint32_t i = 0; i < config.failure_threshold; ++i) {
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

TEST_F(ConfigTest, ThreadSafety) {
  // Test that atomic operations are thread-safe
  constexpr size_t NUM_THREADS = 10;
  constexpr size_t OPS_PER_THREAD = 1000;

  std::vector<std::thread> threads;

  for (size_t t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&]() {
        for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
        // Toggle tracing
          if (i % 2 == 0) {
            enable_tracing();
          } else {
            disable_tracing();
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
