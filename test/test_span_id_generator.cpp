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
#include <set>
#include <thread>
#include <vector>

#include "rmw_robotops/span_id_generator.hpp"

using rmw_robotops::generate_span_id;
using rmw_robotops::generate_trace_id;
using rmw_robotops::init_span_id_generator;

TEST(SpanIdGeneratorTest, GenerateSpanIdFormat) {
  char span_id[17];
  generate_span_id(span_id);

  // Should be 16 hex characters + null terminator
  EXPECT_EQ(16, std::strlen(span_id));

  // Should be valid hex (lowercase)
  for (size_t i = 0; i < 16; ++i) {
    char c = span_id[i];
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
      << "Character at position " << i << " is '" << c << "', not valid hex";
  }
}

TEST(SpanIdGeneratorTest, GenerateTraceIdFormat) {
  char trace_id[33];
  generate_trace_id(trace_id);

  // Should be 32 hex characters + null terminator
  EXPECT_EQ(32, std::strlen(trace_id));

  // Should be valid hex (lowercase)
  for (size_t i = 0; i < 32; ++i) {
    char c = trace_id[i];
    EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
      << "Character at position " << i << " is '" << c << "', not valid hex";
  }
}

TEST(SpanIdGeneratorTest, GenerateUniqueSpanIds) {
  constexpr size_t NUM_IDS = 1000;
  std::set<std::string> ids;

  for (size_t i = 0; i < NUM_IDS; ++i) {
    char span_id[17];
    generate_span_id(span_id);
    ids.insert(span_id);
  }

  // All IDs should be unique
  EXPECT_EQ(NUM_IDS, ids.size());
}

TEST(SpanIdGeneratorTest, GenerateUniqueTraceIds) {
  constexpr size_t NUM_IDS = 1000;
  std::set<std::string> ids;

  for (size_t i = 0; i < NUM_IDS; ++i) {
    char trace_id[33];
    generate_trace_id(trace_id);
    ids.insert(trace_id);
  }

  // All IDs should be unique
  EXPECT_EQ(NUM_IDS, ids.size());
}

TEST(SpanIdGeneratorTest, ThreadSafety) {
  constexpr size_t NUM_THREADS = 10;
  constexpr size_t IDS_PER_THREAD = 100;

  std::vector<std::thread> threads;
  std::vector<std::set<std::string>> thread_ids(NUM_THREADS);

  // Each thread generates IDs
  for (size_t t = 0; t < NUM_THREADS; ++t) {
    threads.emplace_back([&, t]() {
        for (size_t i = 0; i < IDS_PER_THREAD; ++i) {
          char span_id[17];
          generate_span_id(span_id);
          thread_ids[t].insert(span_id);
        }
    });
  }

  // Wait for all threads
  for (auto & thread : threads) {
    thread.join();
  }

  // Collect all IDs from all threads
  std::set<std::string> all_ids;
  for (const auto & thread_set : thread_ids) {
    EXPECT_EQ(IDS_PER_THREAD, thread_set.size());  // Each thread generated unique IDs
    all_ids.insert(thread_set.begin(), thread_set.end());
  }

  // All IDs across all threads should be unique
  EXPECT_EQ(NUM_THREADS * IDS_PER_THREAD, all_ids.size());
}

TEST(SpanIdGeneratorTest, InitIdempotent) {
  // Initialize multiple times (should be safe)
  init_span_id_generator();
  init_span_id_generator();
  init_span_id_generator();

  // Should still work
  char span_id[17];
  generate_span_id(span_id);
  EXPECT_EQ(16, std::strlen(span_id));
}

TEST(SpanIdGeneratorTest, NoAllZeroIds) {
  // xorshift can't generate all-zero, but let's verify
  constexpr size_t NUM_IDS = 10000;

  for (size_t i = 0; i < NUM_IDS; ++i) {
    char span_id[17];
    generate_span_id(span_id);

    // Should not be all zeros
    bool all_zero = true;
    for (size_t j = 0; j < 16; ++j) {
      if (span_id[j] != '0') {
        all_zero = false;
        break;
      }
    }
    EXPECT_FALSE(all_zero) << "Generated all-zero span ID";
  }
}
