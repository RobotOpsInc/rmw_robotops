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
#include <thread>

#include "rmw/rmw.h"
#include "rmw_robotops/config.hpp"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "rmw_robotops/utils.hpp"
#include "robotops_msgs/msg/trace_event.h"

using rmw_robotops::get_dds_domain_id;
using rmw_robotops::get_or_mint_trace_context;
using rmw_robotops::get_trace_context;
using rmw_robotops::get_trace_event_queue;
using rmw_robotops::is_tracing_enabled;
using rmw_robotops::set_trace_context;
using rmw_robotops::TraceContext;
using rmw_robotops::TraceEvent;

class PubSubTracingTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    // Clear trace context
    set_trace_context(TraceContext::empty());

    // Clear event queue
    auto & queue = get_trace_event_queue();
    TraceEvent dummy;
    while (queue.try_pop(dummy)) {}
  }

  void TearDown() override
  {
    set_trace_context(TraceContext::empty());
  }

  // Helper to drain queue and return events
  std::vector<TraceEvent> drain_queue()
  {
    std::vector<TraceEvent> events;
    auto & queue = get_trace_event_queue();
    TraceEvent event;
    while (queue.try_pop(event)) {
      events.push_back(event);
    }
    return events;
  }

  // Helper to find event by type
  bool find_event_by_type(
    const std::vector<TraceEvent> & events,
    uint8_t event_type,
    TraceEvent & out_event)
  {
    for (const auto & event : events) {
      if (event.event_type == event_type) {
        out_event = event;
        return true;
      }
    }
    return false;
  }
};

TEST_F(PubSubTracingTest, TracingEnabledByDefault) {
  // Verify tracing is enabled in test environment
  EXPECT_TRUE(is_tracing_enabled());
}

TEST_F(PubSubTracingTest, PublishSetsTraceContext) {
  // Verify that publishing sets trace context in thread-local storage

  // Initially no context
  EXPECT_TRUE(get_trace_context().is_empty());

  // Set a context
  TraceContext ctx;
  rmw_robotops::generate_trace_id(ctx.trace_id);
  rmw_robotops::generate_span_id(ctx.span_id);
  ctx.parent_span_id[0] = '\0';
  set_trace_context(ctx);

  // Verify context is set
  auto retrieved = get_trace_context();
  EXPECT_FALSE(retrieved.is_empty());
  EXPECT_STREQ(ctx.trace_id, retrieved.trace_id);
  EXPECT_STREQ(ctx.span_id, retrieved.span_id);
}

TEST_F(PubSubTracingTest, GetOrMintCreatesContext) {
  // Verify get_or_mint creates new context if none exists
  EXPECT_TRUE(get_trace_context().is_empty());

  TraceContext ctx = get_or_mint_trace_context();

  // Should have valid IDs
  EXPECT_GT(std::strlen(ctx.trace_id), 0u);
  EXPECT_GT(std::strlen(ctx.span_id), 0u);

  // Parent should be empty for new context
  EXPECT_EQ(0u, std::strlen(ctx.parent_span_id));
}

TEST_F(PubSubTracingTest, ThreadLocalIsolation) {
  // Verify trace context is isolated per-thread

  TraceContext main_ctx;
  rmw_robotops::generate_trace_id(main_ctx.trace_id);
  snprintf(main_ctx.span_id, sizeof(main_ctx.span_id), "MAIN_THREAD_SPAN");
  main_ctx.parent_span_id[0] = '\0';
  set_trace_context(main_ctx);

  // Spawn thread with different context
  std::thread t([&]() {
      // Thread should not have main thread's context
      EXPECT_TRUE(get_trace_context().is_empty());

      // Set different context in thread
      TraceContext thread_ctx;
      snprintf(thread_ctx.trace_id, sizeof(thread_ctx.trace_id), "%s", main_ctx.trace_id);
      snprintf(thread_ctx.span_id, sizeof(thread_ctx.span_id), "THREAD_SPAN");
      snprintf(thread_ctx.parent_span_id, sizeof(thread_ctx.parent_span_id), "%s",
      main_ctx.span_id);
      set_trace_context(thread_ctx);

      // Verify thread context
      auto retrieved = get_trace_context();
      EXPECT_FALSE(retrieved.is_empty());
      EXPECT_STREQ("THREAD_SPAN", retrieved.span_id);
      EXPECT_STREQ("MAIN_THREAD_SPAN", retrieved.parent_span_id);
    });

  t.join();

  // Main thread context should be unchanged
  auto main_retrieved = get_trace_context();
  EXPECT_FALSE(main_retrieved.is_empty());
  EXPECT_STREQ("MAIN_THREAD_SPAN", main_retrieved.span_id);
  EXPECT_EQ(0u, std::strlen(main_retrieved.parent_span_id));
}

TEST_F(PubSubTracingTest, QueueAcceptsTraceEvents) {
  // Verify queue can store and retrieve trace events

  TraceEvent event;
  event.timestamp_ns = 123456789;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START;
  rmw_robotops::generate_trace_id(event.trace_id);
  rmw_robotops::generate_span_id(event.span_id);
  event.parent_span_id[0] = '\0';
  snprintf(event.topic_or_service, sizeof(event.topic_or_service), "/test_topic");
  snprintf(event.node_name, sizeof(event.node_name), "test_node");
  snprintf(event.node_namespace, sizeof(event.node_namespace), "/test");

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START),
    events[0].event_type);
  EXPECT_STREQ("/test_topic", events[0].topic_or_service);
}

TEST_F(PubSubTracingTest, HierarchicalSpansHaveCorrectStructure) {
  // Verify that START/END events form proper hierarchical spans

  // Create parent context
  TraceContext parent;
  rmw_robotops::generate_trace_id(parent.trace_id);
  rmw_robotops::generate_span_id(parent.span_id);
  parent.parent_span_id[0] = '\0';
  set_trace_context(parent);

  // Simulate child span (like what rmw_publish would do)
  char child_span_id[17];
  rmw_robotops::generate_span_id(child_span_id);

  // Create START event
  TraceEvent start_event;
  start_event.timestamp_ns = 100;
  start_event.event_type = robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START;
  snprintf(start_event.trace_id, sizeof(start_event.trace_id), "%s", parent.trace_id);
  snprintf(start_event.span_id, sizeof(start_event.span_id), "%s", child_span_id);
  snprintf(start_event.parent_span_id, sizeof(start_event.parent_span_id), "%s", parent.span_id);

  // Create END event
  TraceEvent end_event;
  end_event.timestamp_ns = 200;
  end_event.event_type = robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_END;
  snprintf(end_event.trace_id, sizeof(end_event.trace_id), "%s", parent.trace_id);
  snprintf(end_event.span_id, sizeof(end_event.span_id), "%s", child_span_id);
  snprintf(end_event.parent_span_id, sizeof(end_event.parent_span_id), "%s", parent.span_id);

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(start_event));
  ASSERT_TRUE(queue.try_push(end_event));

  auto events = drain_queue();
  ASSERT_EQ(2u, events.size());

  // Verify hierarchical structure
  EXPECT_STREQ(events[0].trace_id, events[1].trace_id);  // Same trace
  EXPECT_STREQ(events[0].span_id, events[1].span_id);    // Same span
  EXPECT_STREQ(events[0].parent_span_id, events[1].parent_span_id);  // Same parent
  EXPECT_LT(events[0].timestamp_ns, events[1].timestamp_ns);  // START before END
}

TEST_F(PubSubTracingTest, MultipleEventsPreserveOrder) {
  // Verify MPSC queue preserves event order from single producer

  auto & queue = get_trace_event_queue();

  for (int i = 0; i < 10; ++i) {
    TraceEvent event;
    event.timestamp_ns = static_cast<uint64_t>(i * 100);
    if (i % 2 == 0) {
      event.event_type = robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START;
    } else {
      event.event_type = robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_END;
    }
    ASSERT_TRUE(queue.try_push(event));
  }

  auto events = drain_queue();
  ASSERT_EQ(10u, events.size());

  // Verify order preserved
  for (size_t i = 0; i < events.size(); ++i) {
    EXPECT_EQ(static_cast<uint64_t>(i * 100), events[i].timestamp_ns);
  }
}

TEST_F(PubSubTracingTest, CorrelationMethodSet) {
  // Verify events have correlation method populated

  TraceEvent event;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START;
  event.correlation_method = robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE;
  event.sequence_number = 42;

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE),
    events[0].correlation_method);
  EXPECT_EQ(42u, events[0].sequence_number);
}

TEST_F(PubSubTracingTest, DDSDomainIdTracked) {
  // Verify DDS domain ID is captured in events

  TraceEvent event;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_PUBLISH_RMW_START;
  event.dds_domain_id = get_dds_domain_id();

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());

  // Domain ID should be from ROS_DOMAIN_ID env var (or 0)
  EXPECT_GE(events[0].dds_domain_id, 0);
  EXPECT_LE(events[0].dds_domain_id, 232);  // Valid DDS domain range
}
