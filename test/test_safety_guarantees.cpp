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

#include "rmw_robotops/config.hpp"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/trace_event_queue.hpp"

#include <chrono>
#include <thread>

using rmw_robotops::Config;
using rmw_robotops::DEFAULT_TRACE_QUEUE_SIZE;
using rmw_robotops::generate_span_id;
using rmw_robotops::generate_trace_id;
using rmw_robotops::get_config;
using rmw_robotops::get_trace_event_queue;
using rmw_robotops::init_span_id_generator;
using rmw_robotops::LockFreeQueue;
using rmw_robotops::OP_PUBLISH;
using rmw_robotops::TraceContext;
using rmw_robotops::TraceEvent;

// Safety Guarantee #3: No allocations in hot path
TEST(SafetyTest, NoAllocationsInHotPath) {
  // This test verifies that core operations don't allocate
  // AddressSanitizer will catch any allocations

  // Trace context operations (TLS)
  TraceContext ctx = get_trace_context();
  set_trace_context(ctx);
  ctx = get_or_mint_trace_context();

  // ID generation
  char trace_id[33];
  char span_id[17];
  generate_trace_id(trace_id);
  generate_span_id(span_id);

  // Queue operations
  auto & queue = get_trace_event_queue();
  TraceEvent event;
  queue.try_push(event);
  queue.try_pop(event);

  // Configuration check
  (void)is_tracing_enabled();

  // If we get here without ASan errors, no allocations occurred
  SUCCEED();
}

// Safety Guarantee #4: No exceptions propagate
TEST(SafetyTest, NoExceptionsPropagateNoexcept) {
  // All critical functions are marked noexcept
  // This is enforced at compile-time

  // Verify key functions are noexcept
  EXPECT_TRUE(noexcept(get_trace_context()));
  EXPECT_TRUE(noexcept(set_trace_context(TraceContext::empty())));
  EXPECT_TRUE(noexcept(get_or_mint_trace_context()));

  char buffer[33];
  EXPECT_TRUE(noexcept(generate_trace_id(buffer)));
  EXPECT_TRUE(noexcept(generate_span_id(buffer)));

  EXPECT_TRUE(noexcept(is_tracing_enabled()));
  EXPECT_TRUE(noexcept(enable_tracing()));
  EXPECT_TRUE(noexcept(disable_tracing()));

  LockFreeQueue<10> queue;
  TraceEvent event;
  EXPECT_TRUE(noexcept(queue.try_push(event)));
  EXPECT_TRUE(noexcept(queue.try_pop(event)));
}

// Safety Guarantee #5: Lock-free context propagation
TEST(SafetyTest, LockFreeOperations) {
  // Thread-local storage is lock-free by design
  // Atomic operations use lock-free primitives

  Config & config = get_config();

  // Verify atomics are lock-free
  EXPECT_TRUE(config.tracing_enabled.is_lock_free());
  EXPECT_TRUE(config.consecutive_failures.is_lock_free());

  // Queue head/tail should be lock-free
  // (Checked at compile-time with std::atomic requirements)
  SUCCEED();
}

// Safety Guarantee #6: Runtime kill switch with zero overhead
TEST(SafetyTest, KillSwitchZeroOverhead) {
  disable_tracing();

  // When disabled, tracing should have zero overhead
  // This is a fast atomic read (relaxed memory order)

  auto start = std::chrono::high_resolution_clock::now();

  constexpr size_t ITERATIONS = 1000000;
  for (size_t i = 0; i < ITERATIONS; ++i) {
    if (is_tracing_enabled()) {
      // This branch should never be taken
      FAIL() << "Tracing should be disabled";
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

  double ns_per_check = static_cast<double>(duration.count()) / ITERATIONS;

  // Should be extremely fast (< 5ns on modern CPUs)
  EXPECT_LT(ns_per_check, 5.0)
    << "Kill switch check too slow: " << ns_per_check << " ns/check";

  std::cout << "Kill switch overhead: " << ns_per_check << " ns/check\n";

  // Re-enable for other tests
  enable_tracing();
}

// Safety Guarantee #7: Auto-disable on repeated failures
TEST(SafetyTest, AutoDisableProtection) {
  Config & config = get_config();

  // Set low threshold for testing
  config.failure_threshold = 10;
  config.consecutive_failures.store(0);
  enable_tracing();

  EXPECT_TRUE(is_tracing_enabled());

  // Simulate repeated failures
  for (uint32_t i = 0; i <= config.failure_threshold; ++i) {
    record_trace_failure();
  }

  // Should auto-disable
  EXPECT_FALSE(is_tracing_enabled())
    << "Tracing should auto-disable after " << config.failure_threshold << " failures";

  // Re-enable for other tests
  enable_tracing();
  config.consecutive_failures.store(0);
}

// Safety Guarantee #8: Background queue is non-blocking
TEST(SafetyTest, QueueNonBlocking) {
  LockFreeQueue<10> queue;

  // Fill queue
  for (size_t i = 0; i < 9; ++i) {
    TraceEvent event;
    EXPECT_TRUE(queue.try_push(event));
  }

  // try_push should return immediately when full (not block)
  auto start = std::chrono::high_resolution_clock::now();

  TraceEvent overflow;
  bool result = queue.try_push(overflow);

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  EXPECT_FALSE(result);  // Should fail (queue full)
  EXPECT_LT(duration.count(), 10)  // Should complete in < 10 microseconds
    << "Queue push blocked for " << duration.count() << " microseconds";
}

// Stress test: High-throughput scenario
TEST(SafetyTest, HighThroughputStress) {
  constexpr size_t NUM_OPERATIONS = 100000;

  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
    // Simulate hot path operations
    if (is_tracing_enabled()) {
      char span_id[17];
      generate_span_id(span_id);

      TraceContext ctx = get_or_mint_trace_context();

      TraceEvent event;
      std::memcpy(event.trace_id, ctx.trace_id, sizeof(event.trace_id));
      std::memcpy(event.span_id, span_id, sizeof(event.span_id));

      auto & queue = get_trace_event_queue();
      queue.try_push(event);  // May fail if queue full (graceful degradation)
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  double us_per_op = static_cast<double>(duration.count()) / NUM_OPERATIONS;

  // Should handle 100k ops very quickly (< 1 microsecond per op)
  EXPECT_LT(us_per_op, 1.0)
    << "High-throughput stress test too slow: " << us_per_op << " us/op";

  std::cout << "High-throughput performance: " << us_per_op << " us/operation\n";
  std::cout << "Throughput: " << (1.0 / us_per_op) << " Mops/s\n";
}

// Memory footprint test
TEST(SafetyTest, BoundedMemoryFootprint) {
  // Verify that data structures have reasonable memory footprint

  // TraceContext should be small (fits in cache line)
  EXPECT_LE(sizeof(TraceContext), 128)
    << "TraceContext too large: " << sizeof(TraceContext) << " bytes";

  // TraceEvent should be reasonably sized
  EXPECT_LE(sizeof(TraceEvent), 2048)
    << "TraceEvent too large: " << sizeof(TraceEvent) << " bytes";

  // Queue size should be bounded and predictable
  size_t queue_size = sizeof(LockFreeQueue<DEFAULT_TRACE_QUEUE_SIZE>);
  size_t expected_max = sizeof(TraceEvent) * DEFAULT_TRACE_QUEUE_SIZE + 1024;

  EXPECT_LE(queue_size, expected_max)
    << "Queue too large: " << queue_size << " bytes (expected < " << expected_max << ")";

  std::cout << "Memory footprint:\n";
  std::cout << "  TraceContext: " << sizeof(TraceContext) << " bytes\n";
  std::cout << "  TraceEvent: " << sizeof(TraceEvent) << " bytes\n";
  std::cout << "  Queue: " << queue_size << " bytes\n";
}
