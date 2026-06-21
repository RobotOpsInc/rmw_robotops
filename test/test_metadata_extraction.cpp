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

#include "rcutils/error_handling.h"
#include "rmw_robotops/span_id_generator.hpp"
#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/trace_event_queue.hpp"
#include "rmw_robotops/utils.hpp"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "std_msgs/msg/string.hpp"

using rmw_robotops::generate_span_id;
using rmw_robotops::generate_trace_id;
using rmw_robotops::get_trace_event_queue;
using rmw_robotops::MAX_MESSAGE_TYPE_LENGTH;
using rmw_robotops::MAX_NODE_NAME_LENGTH;
using rmw_robotops::MAX_TOPIC_NAME_LENGTH;
using rmw_robotops::OP_PUBLISH;
using rmw_robotops::OP_SUBSCRIBE;
using rmw_robotops::SPAN_ID_LENGTH;
using rmw_robotops::TraceEvent;

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

// --- resolve_introspection_members() (ROB-403) ---------------------------------------
//
// These tests verify that the introspection-members resolver works for the C++ wrapper
// handle that rclcpp nodes hand the RMW, AND that it never leaves a stale rcutils error
// state behind. The stale-error leak was the root cause of the
// "Handle's typesupport identifier (rosidl_typesupport_cpp) is not supported ...
//  / service's implementation is invalid" log spam seen on real robots.

using rmw_robotops::resolve_introspection_members;

TEST(IntrospectionResolveTest, ResolvesCppTypeSupportHandle) {
  // This is exactly the handle a C++ (rclcpp) node passes to rmw_create_subscription:
  // identifier == "rosidl_typesupport_cpp", whose map contains introspection_cpp (not _c).
  const rosidl_message_type_support_t * cpp_handle =
    rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>();
  ASSERT_NE(nullptr, cpp_handle);

  rcutils_reset_error();
  const rosidl_typesupport_introspection_c__MessageMembers * members =
    resolve_introspection_members(cpp_handle);

  // Before the fix this returned nullptr for C++ nodes (only the _c id was probed),
  // so C++ publishers/subscribers got no tracing type metadata at all.
  ASSERT_NE(nullptr, members);
  EXPECT_STREQ("String", members->message_name_);
  // The C++ introspection variant reports the namespace with C++ scoping ("std_msgs::msg");
  // the C variant reports "std_msgs". We only require that resolution succeeded.
  ASSERT_NE(nullptr, members->message_namespace_);
  EXPECT_NE(nullptr, std::strstr(members->message_namespace_, "std_msgs"));

  // Critical: no stale rcutils error may be left behind for the next thread-local consumer.
  EXPECT_FALSE(rcutils_error_is_set());
}

TEST(IntrospectionResolveTest, NullHandleIsSafeAndLeavesNoError) {
  rcutils_reset_error();
  EXPECT_EQ(nullptr, resolve_introspection_members(nullptr));
  EXPECT_FALSE(rcutils_error_is_set());
}
