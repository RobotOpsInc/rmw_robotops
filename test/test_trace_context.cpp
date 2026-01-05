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

#include <cstring>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "rmw_robotops/trace_context.hpp"

using rmw_robotops::clear_pending_contexts;
using rmw_robotops::collect_pending_contexts;
using rmw_robotops::get_or_mint_trace_context;
using rmw_robotops::get_trace_context;
using rmw_robotops::MAX_PENDING_CONTEXTS;
using rmw_robotops::save_pending_context;
using rmw_robotops::set_trace_context;
using rmw_robotops::SPAN_ID_LENGTH;
using rmw_robotops::TRACE_ID_LENGTH;
using rmw_robotops::TraceContext;

TEST(TraceContextTest, EmptyContext) {
  TraceContext ctx = TraceContext::empty();

  EXPECT_TRUE(ctx.is_empty());
  EXPECT_EQ('\0', ctx.trace_id[0]);
  EXPECT_EQ('\0', ctx.span_id[0]);
  EXPECT_EQ('\0', ctx.parent_span_id[0]);
}

TEST(TraceContextTest, ClearContext) {
  TraceContext ctx;
  std::snprintf(ctx.trace_id, sizeof(ctx.trace_id), "%s", "abc123");
  std::snprintf(ctx.span_id, sizeof(ctx.span_id), "%s", "def456");
  std::snprintf(ctx.parent_span_id, sizeof(ctx.parent_span_id), "%s", "ghi789");

  EXPECT_FALSE(ctx.is_empty());

  ctx.clear();

  EXPECT_TRUE(ctx.is_empty());
  EXPECT_EQ('\0', ctx.trace_id[0]);
  EXPECT_EQ('\0', ctx.span_id[0]);
  EXPECT_EQ('\0', ctx.parent_span_id[0]);
}

TEST(TraceContextTest, GetSetContext) {
  // Initially empty
  TraceContext ctx = get_trace_context();
  EXPECT_TRUE(ctx.is_empty());

  // Set a context
  TraceContext new_ctx;
  std::snprintf(new_ctx.trace_id, sizeof(new_ctx.trace_id), "%s", "0123456789abcdef0123456789abcdef");
  std::snprintf(new_ctx.span_id, sizeof(new_ctx.span_id), "%s", "fedcba9876543210");
  std::snprintf(new_ctx.parent_span_id, sizeof(new_ctx.parent_span_id), "%s", "0011223344556677");

  set_trace_context(new_ctx);

  // Retrieve it
  TraceContext retrieved = get_trace_context();
  EXPECT_FALSE(retrieved.is_empty());
  EXPECT_STREQ("0123456789abcdef0123456789abcdef", retrieved.trace_id);
  EXPECT_STREQ("fedcba9876543210", retrieved.span_id);
  EXPECT_STREQ("0011223344556677", retrieved.parent_span_id);

  // Clear for next test
  TraceContext empty = TraceContext::empty();
  set_trace_context(empty);
}

TEST(TraceContextTest, GetOrMintCreatesNewContext) {
  // Clear any existing context
  TraceContext empty = TraceContext::empty();
  set_trace_context(empty);

  // Should mint a new context
  TraceContext ctx = get_or_mint_trace_context();

  EXPECT_FALSE(ctx.is_empty());
  EXPECT_NE('\0', ctx.trace_id[0]);
  EXPECT_NE('\0', ctx.span_id[0]);
  EXPECT_EQ('\0', ctx.parent_span_id[0]);  // Root span has no parent

  // Verify trace_id and span_id are valid hex (32 and 16 chars)
  EXPECT_EQ(32, std::strlen(ctx.trace_id));
  EXPECT_EQ(16, std::strlen(ctx.span_id));
}

TEST(TraceContextTest, GetOrMintReturnsExistingContext) {
  // Set a context
  TraceContext existing;
  std::snprintf(existing.trace_id, sizeof(existing.trace_id), "%s", "aaaabbbbccccddddeeeeffffgggghhhh");
  std::snprintf(existing.span_id, sizeof(existing.span_id), "%s", "1111222233334444");
  std::snprintf(existing.parent_span_id, sizeof(existing.parent_span_id), "%s", "5555666677778888");
  set_trace_context(existing);

  // Should return the existing context, not mint a new one
  TraceContext ctx = get_or_mint_trace_context();

  EXPECT_STREQ("aaaabbbbccccddddeeeeffffgggghhhh", ctx.trace_id);
  EXPECT_STREQ("1111222233334444", ctx.span_id);
  EXPECT_STREQ("5555666677778888", ctx.parent_span_id);

  // Cleanup
  TraceContext empty = TraceContext::empty();
  set_trace_context(empty);
}

TEST(TraceContextTest, ThreadLocalIsolation) {
  // Main thread context
  TraceContext main_ctx;
  std::strncpy(main_ctx.trace_id, "mainthread0000000000000000000000", TRACE_ID_LENGTH);
  main_ctx.trace_id[TRACE_ID_LENGTH] = '\0';
  std::strncpy(main_ctx.span_id, "mainspan00000000", SPAN_ID_LENGTH);
  main_ctx.span_id[SPAN_ID_LENGTH] = '\0';
  main_ctx.parent_span_id[0] = '\0';
  set_trace_context(main_ctx);

  // Verify main thread
  TraceContext main_retrieved = get_trace_context();
  EXPECT_STREQ("mainthread0000000000000000000000", main_retrieved.trace_id);

  // Spawn thread with different context
  std::thread t([&]() {
    // Thread should start with empty context (TLS isolation)
    TraceContext thread_ctx = get_trace_context();
    EXPECT_TRUE(thread_ctx.is_empty());

    // Set different context in thread
    TraceContext new_ctx;
    std::strncpy(new_ctx.trace_id, "threadcontext000000000000000000", TRACE_ID_LENGTH);
    new_ctx.trace_id[TRACE_ID_LENGTH] = '\0';
    std::strncpy(new_ctx.span_id, "threadspan000000", SPAN_ID_LENGTH);
    new_ctx.span_id[SPAN_ID_LENGTH] = '\0';
    new_ctx.parent_span_id[0] = '\0';
    set_trace_context(new_ctx);

    // Verify thread context
    TraceContext thread_retrieved = get_trace_context();
    EXPECT_STREQ("threadcontext000000000000000000", thread_retrieved.trace_id);
  });

  t.join();

  // Main thread context should be unchanged
  TraceContext main_after = get_trace_context();
  EXPECT_STREQ("mainthread0000000000000000000000", main_after.trace_id);

  // Cleanup
  TraceContext empty = TraceContext::empty();
  set_trace_context(empty);
}

TEST(TraceContextTest, PendingContexts) {
  clear_pending_contexts();

  // Add some pending contexts
  TraceContext ctx1;
  std::snprintf(ctx1.trace_id, sizeof(ctx1.trace_id), "%s", "pending1000000000000000000000000");
  std::snprintf(ctx1.span_id, sizeof(ctx1.span_id), "%s", "span100000000000");

  TraceContext ctx2;
  std::snprintf(ctx2.trace_id, sizeof(ctx2.trace_id), "%s", "pending2000000000000000000000000");
  std::snprintf(ctx2.span_id, sizeof(ctx2.span_id), "%s", "span200000000000");

  EXPECT_TRUE(save_pending_context(ctx1));
  EXPECT_TRUE(save_pending_context(ctx2));

  // Collect pending contexts
  TraceContext collected[10];
  size_t count = collect_pending_contexts(collected, 10);

  EXPECT_EQ(2, count);
  EXPECT_STREQ("pending1000000000000000000000000", collected[0].trace_id);
  EXPECT_STREQ("pending2000000000000000000000000", collected[1].trace_id);

  // Should be cleared after collection
  count = collect_pending_contexts(collected, 10);
  EXPECT_EQ(0, count);
}

TEST(TraceContextTest, PendingContextsBufferFull) {
  clear_pending_contexts();

  // Fill up the pending buffer
  for (size_t i = 0; i < MAX_PENDING_CONTEXTS; ++i) {
    TraceContext ctx;
    std::snprintf(ctx.trace_id, sizeof(ctx.trace_id), "trace%02zu", i);
    EXPECT_TRUE(save_pending_context(ctx));
  }

  // Next one should fail (buffer full)
  TraceContext overflow;
  std::snprintf(overflow.trace_id, sizeof(overflow.trace_id), "%s", "overflow");
  EXPECT_FALSE(save_pending_context(overflow));

  // Cleanup
  clear_pending_contexts();
}

TEST(TraceContextTest, CollectPendingContextsTruncation) {
  clear_pending_contexts();

  // Add 5 contexts
  for (size_t i = 0; i < 5; ++i) {
    TraceContext ctx;
    std::snprintf(ctx.trace_id, sizeof(ctx.trace_id), "trace%zu", i);
    save_pending_context(ctx);
  }

  // Collect with small buffer (only 3)
  TraceContext collected[3];
  size_t count = collect_pending_contexts(collected, 3);

  EXPECT_EQ(3, count);  // Should truncate to buffer size
  EXPECT_STREQ("trace0", collected[0].trace_id);
  EXPECT_STREQ("trace1", collected[1].trace_id);
  EXPECT_STREQ("trace2", collected[2].trace_id);

  // Cleanup
  clear_pending_contexts();
}
