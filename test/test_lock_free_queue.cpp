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

#include "rmw_robotops/trace_event_queue.hpp"

using namespace rmw_robotops;

TEST(LockFreeQueueTest, EmptyOnConstruction) {
  LockFreeQueue<10> queue;

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.size());
  EXPECT_EQ(10, queue.capacity());
}

TEST(LockFreeQueueTest, PushPop) {
  LockFreeQueue<10> queue;

  TraceEvent event1;
  std::strcpy(event1.trace_id, "trace1");
  std::strcpy(event1.topic_or_service, "/topic1");
  event1.operation = OP_PUBLISH;

  // Push
  EXPECT_TRUE(queue.try_push(event1));
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(1, queue.size());

  // Pop
  TraceEvent popped;
  EXPECT_TRUE(queue.try_pop(popped));
  EXPECT_STREQ("trace1", popped.trace_id);
  EXPECT_STREQ("/topic1", popped.topic_or_service);
  EXPECT_EQ(OP_PUBLISH, popped.operation);

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.size());
}

TEST(LockFreeQueueTest, PopFromEmpty) {
  LockFreeQueue<10> queue;

  TraceEvent event;
  EXPECT_FALSE(queue.try_pop(event));
}

TEST(LockFreeQueueTest, FillQueue) {
  LockFreeQueue<10> queue;

  // Fill to capacity - 1 (ring buffer implementation)
  for (size_t i = 0; i < 9; ++i) {
    TraceEvent event;
    std::snprintf(event.trace_id, sizeof(event.trace_id), "trace%zu", i);
    EXPECT_TRUE(queue.try_push(event));
  }

  EXPECT_EQ(9, queue.size());

  // Next push should fail (full)
  TraceEvent overflow;
  std::strcpy(overflow.trace_id, "overflow");
  EXPECT_FALSE(queue.try_push(overflow));
}

TEST(LockFreeQueueTest, PushPopSequence) {
  LockFreeQueue<5> queue;

  // Push 3
  for (size_t i = 0; i < 3; ++i) {
    TraceEvent event;
    std::snprintf(event.trace_id, sizeof(event.trace_id), "trace%zu", i);
    EXPECT_TRUE(queue.try_push(event));
  }

  // Pop 2
  TraceEvent popped;
  EXPECT_TRUE(queue.try_pop(popped));
  EXPECT_STREQ("trace0", popped.trace_id);
  EXPECT_TRUE(queue.try_pop(popped));
  EXPECT_STREQ("trace1", popped.trace_id);

  // Push 2 more
  for (size_t i = 3; i < 5; ++i) {
    TraceEvent event;
    std::snprintf(event.trace_id, sizeof(event.trace_id), "trace%zu", i);
    EXPECT_TRUE(queue.try_push(event));
  }

  // Should have 3 items (1 remaining + 2 new)
  EXPECT_EQ(3, queue.size());

  // Pop all
  EXPECT_TRUE(queue.try_pop(popped));
  EXPECT_STREQ("trace2", popped.trace_id);
  EXPECT_TRUE(queue.try_pop(popped));
  EXPECT_STREQ("trace3", popped.trace_id);
  EXPECT_TRUE(queue.try_pop(popped));
  EXPECT_STREQ("trace4", popped.trace_id);

  EXPECT_TRUE(queue.empty());
}

TEST(LockFreeQueueTest, MultiThreadedProducerConsumer) {
  LockFreeQueue<1000> queue;
  constexpr size_t NUM_ITEMS = 10000;

  std::atomic<bool> consumer_done{false};
  std::atomic<size_t> consumed{0};

  // Consumer thread
  std::thread consumer([&]() {
    while (consumed < NUM_ITEMS) {
      TraceEvent event;
      if (queue.try_pop(event)) {
        consumed++;
      } else {
        std::this_thread::yield();
      }
    }
    consumer_done = true;
  });

  // Producer thread
  std::thread producer([&]() {
    for (size_t i = 0; i < NUM_ITEMS; ++i) {
      TraceEvent event;
      std::snprintf(event.trace_id, sizeof(event.trace_id), "trace%zu", i);

      while (!queue.try_push(event)) {
        std::this_thread::yield();  // Queue full, retry
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_TRUE(consumer_done);
  EXPECT_EQ(NUM_ITEMS, consumed);
  EXPECT_TRUE(queue.empty());
}

TEST(LockFreeQueueTest, MultipleProducersSingleConsumer) {
  LockFreeQueue<1000> queue;
  constexpr size_t NUM_PRODUCERS = 4;
  constexpr size_t ITEMS_PER_PRODUCER = 2500;
  constexpr size_t TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

  std::atomic<size_t> consumed{0};
  std::vector<std::thread> producers;

  // Consumer thread
  std::thread consumer([&]() {
    while (consumed < TOTAL_ITEMS) {
      TraceEvent event;
      if (queue.try_pop(event)) {
        consumed++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  // Multiple producer threads
  for (size_t p = 0; p < NUM_PRODUCERS; ++p) {
    producers.emplace_back([&, p]() {
      for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        TraceEvent event;
        std::snprintf(event.trace_id, sizeof(event.trace_id), "p%zu_i%zu", p, i);

        while (!queue.try_push(event)) {
          std::this_thread::yield();
        }
      }
    });
  }

  // Wait for all producers
  for (auto & producer : producers) {
    producer.join();
  }

  // Wait for consumer
  consumer.join();

  EXPECT_EQ(TOTAL_ITEMS, consumed);
  EXPECT_TRUE(queue.empty());
}

TEST(LockFreeQueueTest, GlobalQueueAccessible) {
  auto & queue = get_trace_event_queue();

  EXPECT_EQ(DEFAULT_TRACE_QUEUE_SIZE, queue.capacity());

  // Should be usable
  TraceEvent event;
  std::strcpy(event.trace_id, "global_test");
  EXPECT_TRUE(queue.try_push(event));

  TraceEvent popped;
  EXPECT_TRUE(queue.try_pop(popped));
  EXPECT_STREQ("global_test", popped.trace_id);
}

TEST(LockFreeQueueTest, SpanLinksInEvent) {
  LockFreeQueue<10> queue;

  TraceEvent event;
  std::strcpy(event.trace_id, "main_trace");
  std::strcpy(event.topic_or_service, "/output");
  event.operation = OP_PUBLISH;

  // Add span links (fan-in) - format: "trace_id:span_id"
  event.span_link_count = 2;
  std::strcpy(event.span_links[0], "input1_trace:input1_span");
  std::strcpy(event.span_links[1], "input2_trace:input2_span");

  EXPECT_TRUE(queue.try_push(event));

  TraceEvent popped;
  EXPECT_TRUE(queue.try_pop(popped));

  EXPECT_EQ(2, popped.span_link_count);
  EXPECT_STREQ("input1_trace:input1_span", popped.span_links[0]);
  EXPECT_STREQ("input2_trace:input2_span", popped.span_links[1]);
}
