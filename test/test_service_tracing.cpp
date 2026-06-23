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

class ServiceTracingTest : public ::testing::Test
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

TEST_F(ServiceTracingTest, ServiceRequestEventStructure) {
  // Verify SERVICE_REQUEST event has correct structure

  TraceEvent event;
  event.timestamp_ns = 123456789;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
  rmw_robotops::generate_trace_id(event.trace_id);
  rmw_robotops::generate_span_id(event.span_id);
  event.parent_span_id[0] = '\0';  // Root span for service request
  event.dds_domain_id = get_dds_domain_id();
  event.correlation_method = robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE;
  event.sequence_number = 42;
  snprintf(event.topic_or_service, sizeof(event.topic_or_service), "/test_service");

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST),
    events[0].event_type);
  EXPECT_STREQ("/test_service", events[0].topic_or_service);
  EXPECT_EQ(42u, events[0].sequence_number);
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE),
    events[0].correlation_method);
}

TEST_F(ServiceTracingTest, ServiceResponseEventStructure) {
  // Verify SERVICE_RESPONSE event has correct structure

  TraceEvent event;
  event.timestamp_ns = 987654321;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_RESPONSE;
  rmw_robotops::generate_trace_id(event.trace_id);
  rmw_robotops::generate_span_id(event.span_id);
  snprintf(event.parent_span_id, sizeof(event.parent_span_id), "PARENT_SPAN_ID");
  event.dds_domain_id = get_dds_domain_id();
  event.correlation_method = robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE;
  event.sequence_number = 100;
  snprintf(event.topic_or_service, sizeof(event.topic_or_service), "/test_service");

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__EVENT_SERVICE_RESPONSE),
    events[0].event_type);
  EXPECT_STREQ("PARENT_SPAN_ID", events[0].parent_span_id);
}

TEST_F(ServiceTracingTest, ServiceTakeLeavesTraceEmptyForCorrelation) {
  // ROB-406: the service take path must NO LONGER mint a fresh root trace.
  // Previously rmw_take_request minted a new trace_id + empty parent on every
  // take, guaranteeing every service request rendered as a separate root span.
  // The new contract leaves trace_id AND parent_span_id EMPTY so the robot_agent
  // CorrelationEngine can rewrite them to the client-send's trace (mirroring the
  // subscribe path). The local thread context keeps the span_id (with an empty
  // trace_id) so send_response can pair its RESPONSE to this REQUEST.

  EXPECT_TRUE(get_trace_context().is_empty());

  // Simulate the new service take behavior: span_id set, trace_id left empty.
  TraceContext new_context;
  new_context.trace_id[0] = '\0';  // Empty → agent correlates
  rmw_robotops::generate_span_id(new_context.span_id);
  new_context.parent_span_id[0] = '\0';
  set_trace_context(new_context);

  auto retrieved = get_trace_context();
  // span_id is present so send_response can pair with this request...
  EXPECT_GT(std::strlen(retrieved.span_id), 0u);
  // ...but trace_id is intentionally empty (the agent fills it in).
  EXPECT_EQ(0u, std::strlen(retrieved.trace_id));
  EXPECT_EQ(0u, std::strlen(retrieved.parent_span_id));
}

TEST_F(ServiceTracingTest, ServiceRequestCarriesConsumerDirectionAndHash) {
  // ROB-406: a service-take SERVICE_REQUEST event is the CONSUMER end of the
  // RPC and must carry DIRECTION_CONSUMER + a content hash + empty trace/parent
  // so the agent correlates it to the client-send by content hash.
  TraceEvent event;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
  event.trace_id[0] = '\0';
  event.parent_span_id[0] = '\0';
  rmw_robotops::generate_span_id(event.span_id);
  event.correlation_method = robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;
  event.direction = robotops_msgs__msg__TraceEvent__DIRECTION_CONSUMER;
  event.content_hash = 0xfeed'face'dead'beefULL;
  snprintf(event.topic_or_service, sizeof(event.topic_or_service), "/test_service");

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ(0u, std::strlen(events[0].trace_id));        // empty → correlated
  EXPECT_EQ(0u, std::strlen(events[0].parent_span_id));  // empty → correlated
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH),
    events[0].correlation_method);
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__DIRECTION_CONSUMER),
    events[0].direction);
  EXPECT_EQ(0xfeed'face'dead'beefULL, events[0].content_hash);
}

TEST_F(ServiceTracingTest, ClientRequestCarriesProducerDirection) {
  // ROB-406: the client send_request SERVICE_REQUEST event is the PRODUCER end
  // and seeds the correlation window in the agent.
  TraceEvent event;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
  event.direction = robotops_msgs__msg__TraceEvent__DIRECTION_PRODUCER;
  event.correlation_method = robotops_msgs__msg__TraceEvent__CORRELATION_FALLBACK_HASH;
  event.content_hash = 0x0123'4567'89ab'cdefULL;

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_EQ(
    static_cast<uint8_t>(robotops_msgs__msg__TraceEvent__DIRECTION_PRODUCER),
    events[0].direction);
  EXPECT_EQ(0x0123'4567'89ab'cdefULL, events[0].content_hash);
}

TEST_F(ServiceTracingTest, ServiceResponseContinuesTrace) {
  // Verify service response continues the trace from request

  // Set up trace context from service request
  TraceContext request_ctx;
  rmw_robotops::generate_trace_id(request_ctx.trace_id);
  rmw_robotops::generate_span_id(request_ctx.span_id);
  request_ctx.parent_span_id[0] = '\0';  // Root span from client
  set_trace_context(request_ctx);

  // Service response should continue the same trace
  auto response_ctx = get_trace_context();
  EXPECT_STREQ(request_ctx.trace_id, response_ctx.trace_id);

  // Response creates child span
  char response_span_id[17];
  rmw_robotops::generate_span_id(response_span_id);

  TraceEvent response_event;
  response_event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_RESPONSE;
  snprintf(response_event.trace_id, sizeof(response_event.trace_id), "%s",
    response_ctx.trace_id);
  snprintf(response_event.span_id, sizeof(response_event.span_id), "%s", response_span_id);
  snprintf(response_event.parent_span_id, sizeof(response_event.parent_span_id), "%s",
    request_ctx.span_id);

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(response_event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_STREQ(request_ctx.trace_id, events[0].trace_id);
  EXPECT_STREQ(request_ctx.span_id, events[0].parent_span_id);
}

TEST_F(ServiceTracingTest, SequenceNumberCorrelation) {
  // Verify request and response use sequence numbers for correlation

  uint64_t sequence_id = 12345;

  // Client sends request
  TraceEvent request_event;
  request_event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
  request_event.correlation_method =
    robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE;
  request_event.sequence_number = sequence_id;

  // Client receives response
  TraceEvent response_event;
  response_event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_RESPONSE;
  response_event.correlation_method =
    robotops_msgs__msg__TraceEvent__CORRELATION_FASTDDS_SEQUENCE;
  response_event.sequence_number = sequence_id;  // Same sequence number

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(request_event));
  ASSERT_TRUE(queue.try_push(response_event));

  auto events = drain_queue();
  ASSERT_EQ(2u, events.size());
  EXPECT_EQ(events[0].sequence_number, events[1].sequence_number);
}

TEST_F(ServiceTracingTest, MultipleServiceCallsIsolated) {
  // Verify multiple service calls don't interfere

  auto & queue = get_trace_event_queue();

  for (int i = 0; i < 5; ++i) {
    TraceEvent request;
    request.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
    rmw_robotops::generate_trace_id(request.trace_id);
    request.sequence_number = static_cast<uint64_t>(i);

    TraceEvent response;
    response.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_RESPONSE;
    snprintf(response.trace_id, sizeof(response.trace_id), "%s", request.trace_id);
    response.sequence_number = static_cast<uint64_t>(i);

    ASSERT_TRUE(queue.try_push(request));
    ASSERT_TRUE(queue.try_push(response));
  }

  auto events = drain_queue();
  ASSERT_EQ(10u, events.size());

  // Verify request-response pairs have matching trace IDs
  for (size_t i = 0; i < 5; ++i) {
    size_t req_idx = i * 2;
    size_t resp_idx = i * 2 + 1;
    EXPECT_STREQ(events[req_idx].trace_id, events[resp_idx].trace_id);
    EXPECT_EQ(events[req_idx].sequence_number, events[resp_idx].sequence_number);
  }
}

TEST_F(ServiceTracingTest, ServiceNameTracked) {
  // Verify service name is captured in events

  TraceEvent event;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
  snprintf(event.topic_or_service, sizeof(event.topic_or_service), "/my/service/name");

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_STREQ("/my/service/name", events[0].topic_or_service);
}

TEST_F(ServiceTracingTest, NodeMetadataTracked) {
  // Verify node name and namespace are captured

  TraceEvent event;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
  snprintf(event.node_name, sizeof(event.node_name), "service_node");
  snprintf(event.node_namespace, sizeof(event.node_namespace), "/robot");

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_STREQ("service_node", events[0].node_name);
  EXPECT_STREQ("/robot", events[0].node_namespace);
}

TEST_F(ServiceTracingTest, DDSDomainIdTrackedForServices) {
  // Verify DDS domain ID is captured for service events

  TraceEvent event;
  event.event_type = robotops_msgs__msg__TraceEvent__EVENT_SERVICE_REQUEST;
  event.dds_domain_id = get_dds_domain_id();

  auto & queue = get_trace_event_queue();
  ASSERT_TRUE(queue.try_push(event));

  auto events = drain_queue();
  ASSERT_EQ(1u, events.size());
  EXPECT_GE(events[0].dds_domain_id, 0);
  EXPECT_LE(events[0].dds_domain_id, 232);  // Valid DDS domain range
}
