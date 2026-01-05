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

#include "rmw_robotops/trace_event_queue.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/span_id_generator.hpp"

#include <cstring>

using namespace rmw_robotops;

TEST(MetadataExtractionTest, TraceEventFieldsInitialization) {
  // Verify that TraceEvent struct has all required metadata fields
  TraceEvent event;

  // Initialize all fields
  event.timestamp_ns = 1234567890;

  generate_trace_id(event.trace_id);
  generate_span_id(event.span_id);
  event.parent_span_id[0] = '\0';

  event.span_link_count = 0;

  event.operation = OP_PUBLISH;
  std::strncpy(event.topic_or_service, "/test_topic", MAX_TOPIC_NAME_LENGTH - 1);
  event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

  // Metadata fields (extracted from RMW)
  std::strncpy(event.node_name, "test_node", MAX_NODE_NAME_LENGTH - 1);
  event.node_name[MAX_NODE_NAME_LENGTH - 1] = '\0';

  std::strncpy(event.node_namespace, "/test_namespace", MAX_NODE_NAME_LENGTH - 1);
  event.node_namespace[MAX_NODE_NAME_LENGTH - 1] = '\0';

  std::strncpy(event.message_type, "std_msgs/msg/String", MAX_MESSAGE_TYPE_LENGTH - 1);
  event.message_type[MAX_MESSAGE_TYPE_LENGTH - 1] = '\0';

  // DDS correlation fields (not yet implemented)
  event.publisher_gid[0] = '\0';
  event.sequence_number = 0;
  event.message_size_bytes = 0;

  // Verify fields are set correctly
  EXPECT_EQ(1234567890, event.timestamp_ns);
  EXPECT_STREQ("/test_topic", event.topic_or_service);
  EXPECT_STREQ("test_node", event.node_name);
  EXPECT_STREQ("/test_namespace", event.node_namespace);
  EXPECT_STREQ("std_msgs/msg/String", event.message_type);
  EXPECT_EQ(OP_PUBLISH, event.operation);
  EXPECT_EQ(0, event.span_link_count);
  EXPECT_EQ(0, event.sequence_number);
  EXPECT_EQ(0, event.message_size_bytes);
}

TEST(MetadataExtractionTest, QueueHandlesCompleteEvents) {
  // Verify that lock-free queue can handle events with all metadata
  auto & queue = get_trace_event_queue();

  // Clear queue
  TraceEvent dummy;
  while (queue.try_pop(dummy)) {}

  // Create event with full metadata
  TraceEvent event;
  event.timestamp_ns = 9876543210;

  generate_trace_id(event.trace_id);
  generate_span_id(event.span_id);
  std::strncpy(event.parent_span_id, "0000000000000000", SPAN_ID_LENGTH);
  event.parent_span_id[SPAN_ID_LENGTH] = '\0';

  event.span_link_count = 0;

  event.operation = OP_SUBSCRIBE;
  std::strncpy(event.topic_or_service, "/sensor/data", MAX_TOPIC_NAME_LENGTH - 1);
  event.topic_or_service[MAX_TOPIC_NAME_LENGTH - 1] = '\0';

  std::strncpy(event.node_name, "sensor_node", MAX_NODE_NAME_LENGTH - 1);
  event.node_name[MAX_NODE_NAME_LENGTH - 1] = '\0';

  std::strncpy(event.node_namespace, "/sensors", MAX_NODE_NAME_LENGTH - 1);
  event.node_namespace[MAX_NODE_NAME_LENGTH - 1] = '\0';

  std::strncpy(event.message_type, "sensor_msgs/msg/LaserScan", MAX_MESSAGE_TYPE_LENGTH - 1);
  event.message_type[MAX_MESSAGE_TYPE_LENGTH - 1] = '\0';

  event.publisher_gid[0] = '\0';
  event.sequence_number = 42;
  event.message_size_bytes = 1024;

  // Push to queue
  ASSERT_TRUE(queue.try_push(event));

  // Pop from queue
  TraceEvent retrieved;
  ASSERT_TRUE(queue.try_pop(retrieved));

  // Verify all metadata preserved
  EXPECT_EQ(9876543210, retrieved.timestamp_ns);
  EXPECT_EQ(OP_SUBSCRIBE, retrieved.operation);
  EXPECT_STREQ("/sensor/data", retrieved.topic_or_service);
  EXPECT_STREQ("sensor_node", retrieved.node_name);
  EXPECT_STREQ("/sensors", retrieved.node_namespace);
  EXPECT_STREQ("sensor_msgs/msg/LaserScan", retrieved.message_type);
  EXPECT_EQ(42, retrieved.sequence_number);
  EXPECT_EQ(1024, retrieved.message_size_bytes);
}

TEST(MetadataExtractionTest, MetadataFieldsHaveCorrectLengths) {
  // Verify buffer sizes are sufficient for realistic data
  TraceEvent event;

  // Test maximum length node names
  char long_node_name[MAX_NODE_NAME_LENGTH];
  std::memset(long_node_name, 'a', MAX_NODE_NAME_LENGTH - 1);
  long_node_name[MAX_NODE_NAME_LENGTH - 1] = '\0';

  std::memcpy(event.node_name, long_node_name, MAX_NODE_NAME_LENGTH - 1);
  event.node_name[MAX_NODE_NAME_LENGTH - 1] = '\0';

  EXPECT_EQ(MAX_NODE_NAME_LENGTH - 1, std::strlen(event.node_name));

  // Test maximum length message types
  char long_message_type[MAX_MESSAGE_TYPE_LENGTH];
  std::memset(long_message_type, 'b', MAX_MESSAGE_TYPE_LENGTH - 1);
  long_message_type[MAX_MESSAGE_TYPE_LENGTH - 1] = '\0';

  std::memcpy(event.message_type, long_message_type, MAX_MESSAGE_TYPE_LENGTH - 1);
  event.message_type[MAX_MESSAGE_TYPE_LENGTH - 1] = '\0';

  EXPECT_EQ(MAX_MESSAGE_TYPE_LENGTH - 1, std::strlen(event.message_type));
}
