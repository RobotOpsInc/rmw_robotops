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

#include <cstring>

#include "rmw_robotops/correlation_strategy.hpp"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "robotops_msgs/msg/trace_event.h"

using rmw_robotops::create_correlation_strategy;
using rmw_robotops::CorrelationStrategy;
using rmw_robotops::TraceContext;

class CorrelationStrategyTest : public ::testing::Test
{
protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(CorrelationStrategyTest, StrategyCreation) {
  // Verify correlation strategy can be created
  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);
}

TEST_F(CorrelationStrategyTest, HasName) {
  // Verify strategy provides a name for diagnostics
  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  const char * name = strategy->get_name();
  ASSERT_NE(nullptr, name);
  EXPECT_GT(std::strlen(name), 0u);
}

TEST_F(CorrelationStrategyTest, HasCorrelationMethod) {
  // Verify strategy provides correlation method enum
  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  uint8_t method = strategy->get_correlation_method();
  // Should use content hash correlation (DDS-agnostic)
  EXPECT_EQ(robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH, method);
}

TEST_F(CorrelationStrategyTest, IsDeterministicProperty) {
  // Verify strategy reports determinism property
  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  // Should return either true or false (not crash)
  bool deterministic = strategy->is_deterministic();
  (void)deterministic;  // Use the value to avoid warning
}

TEST_F(CorrelationStrategyTest, InjectContextSignature) {
  // Verify inject_context method exists with correct signature
  // Note: We can't actually test it without mocking RMW structures,
  // but we can verify it compiles and doesn't crash with nullptr

  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  TraceContext ctx;
  rmw_robotops::generate_trace_id(ctx.trace_id);
  rmw_robotops::generate_span_id(ctx.span_id);

  // Call with nullptr (should safely handle it due to noexcept)
  bool result = strategy->inject_context(nullptr, ctx, nullptr, 0);
  (void)result;  // Currently returns false for unimplemented
}

TEST_F(CorrelationStrategyTest, ExtractContextSignature) {
  // Verify extract_context method exists with correct signature

  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  TraceContext ctx;
  rmw_robotops::CorrelationMetadata metadata;

  // Call with nullptr (should safely handle it due to noexcept)
  bool result = strategy->extract_context(nullptr, nullptr, nullptr, nullptr, 0, ctx, metadata);
  (void)result;  // Currently returns false for unimplemented
}

TEST_F(CorrelationStrategyTest, CorrelationMethodConsistency) {
  // Verify correlation method is consistent across calls
  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  uint8_t method1 = strategy->get_correlation_method();
  uint8_t method2 = strategy->get_correlation_method();
  EXPECT_EQ(method1, method2);
}

TEST_F(CorrelationStrategyTest, NameConsistency) {
  // Verify name is consistent across calls
  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  const char * name1 = strategy->get_name();
  const char * name2 = strategy->get_name();
  EXPECT_STREQ(name1, name2);
}

TEST_F(CorrelationStrategyTest, DeterminismConsistency) {
  // Verify determinism property is consistent
  auto strategy = create_correlation_strategy();
  ASSERT_NE(nullptr, strategy);

  bool det1 = strategy->is_deterministic();
  bool det2 = strategy->is_deterministic();
  EXPECT_EQ(det1, det2);
}
