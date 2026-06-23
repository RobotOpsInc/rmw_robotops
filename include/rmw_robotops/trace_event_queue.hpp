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

#ifndef RMW_ROBOTOPS__TRACE_EVENT_QUEUE_HPP_
#define RMW_ROBOTOPS__TRACE_EVENT_QUEUE_HPP_

#include "rmw_robotops/trace_context.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

namespace rmw_robotops
{

/// Operation types for trace events
/// Must match robotops_msgs/msg/TraceEvent.msg constants
enum OperationType : uint8_t
{
  OP_PUBLISH = 1,
  OP_SUBSCRIBE = 2,
  OP_SERVICE_REQUEST = 3,
  OP_SERVICE_RESPONSE = 4,
  OP_ACTION_GOAL_SENT = 5,
  OP_ACTION_GOAL_RECEIVED = 6,
  OP_ACTION_FEEDBACK = 7,
  OP_ACTION_RESULT = 8,
  OP_ACTION_CANCEL = 9,
  OP_ACTION_GOAL_REJECTED = 10,
};

/// Maximum string lengths (pre-allocated, no dynamic allocation)
constexpr size_t MAX_TOPIC_NAME_LENGTH = 256;
constexpr size_t MAX_NODE_NAME_LENGTH = 256;
constexpr size_t MAX_MESSAGE_TYPE_LENGTH = 128;
constexpr size_t MAX_PUBLISHER_GID_LENGTH = 64;  // DDS GUID as hex string

/// Maximum number of span links per event (fan-in scenarios)
constexpr size_t MAX_SPAN_LINKS = 8;

/// Trace event emitted by RMW interception
/// Sent to background thread for publishing to /robotops/trace_events
/// Matches robotops_msgs/msg/TraceEvent.msg schema (0.2.1+)
struct TraceEvent
{
  // Timestamp (nanoseconds since epoch, converted to builtin_interfaces/Time)
  uint64_t timestamp_ns;

  // Event type for hierarchical span reconstruction
  uint8_t event_type;  // EVENT_PUBLISH_RMW_START, EVENT_PUBLISH_RMW_END, etc.

  // Trace identification
  char trace_id[TRACE_ID_LENGTH + 1];        // UUID, shared across related spans
  char span_id[SPAN_ID_LENGTH + 1];          // UUID, unique to this span
  char parent_span_id[SPAN_ID_LENGTH + 1];   // UUID of parent span (empty if root)

  // Span links for fan-in scenarios (multiple inputs → one output)
  // Each link formatted as "trace_id:span_id"
  size_t span_link_count;
  // +3 for ':' and '\0' (safe for max length trace_id + span_id)
  char span_links[MAX_SPAN_LINKS][TRACE_ID_LENGTH + SPAN_ID_LENGTH + 3];

  // Topic/service identification
  char topic_or_service[MAX_TOPIC_NAME_LENGTH];
  char node_name[MAX_NODE_NAME_LENGTH];           // Fully qualified (/namespace/node)
  char node_namespace[MAX_NODE_NAME_LENGTH];

  // For correlation (matching publish to subscribe)
  char publisher_gid[MAX_PUBLISHER_GID_LENGTH];   // DDS publisher GUID (hex string)
  uint64_t sequence_number;                       // Message sequence number (DDS-specific)
  int64_t source_timestamp_ns;                    // Publisher's timestamp

  // Content hash for content-based correlation
  // FNV-1a hash computed via message introspection (0 if not computed)
  uint64_t content_hash;

  // Pointer for span reconstruction (correlate with ros2_tracing)
  uint64_t msg_ptr;                               // Message pointer for event correlation

  // Metadata
  char message_type[MAX_MESSAGE_TYPE_LENGTH];     // e.g., "sensor_msgs/msg/Image"
  uint32_t message_size_bytes;                    // Serialized message size

  // DDS domain for multi-domain filtering
  uint32_t dds_domain_id;

  // Correlation metadata
  // Note: CORRELATION_FALLBACK_HASH is a historical name from robotops_msgs
  // It's now the primary/only correlation method (content-based via introspection)
  uint8_t correlation_method;  // From robotops_msgs TraceEvent constants

  // Producer-vs-consumer direction for request/response-style events (ROB-406).
  // For services/actions the same event_type is emitted on both ends of the RPC
  // (client send_request and server take_request are both EVENT_SERVICE_REQUEST),
  // so the agent's CorrelationEngine needs this to tell who SEEDS the window
  // (producer) from who CORRELATES against it (consumer).
  // Values match robotops_msgs::msg::TraceEvent::DIRECTION_* constants:
  //   0 = DIRECTION_UNSPECIFIED (pub/sub: direction implicit in event_type)
  //   1 = DIRECTION_PRODUCER    (client send_request / send_response)
  //   2 = DIRECTION_CONSUMER    (service take_request / client take_response)
  uint8_t direction;

  // Legacy operation field (deprecated, use event_type instead)
  OperationType operation;

  TraceEvent() noexcept
  : timestamp_ns(0),
    event_type(0),
    span_link_count(0),
    sequence_number(0),
    source_timestamp_ns(0),
    content_hash(0),
    msg_ptr(0),
    message_size_bytes(0),
    dds_domain_id(0),
    correlation_method(0),
    direction(0),
    operation(OP_PUBLISH)
  {
    trace_id[0] = '\0';
    span_id[0] = '\0';
    parent_span_id[0] = '\0';
    topic_or_service[0] = '\0';
    node_name[0] = '\0';
    node_namespace[0] = '\0';
    publisher_gid[0] = '\0';
    message_type[0] = '\0';
  }
};

/// Lock-free bounded queue for trace events
/// MPSC: Multiple producers (robot threads) → Single consumer (background thread)
/// Uses ring buffer with atomic head/tail pointers
///
/// Safety guarantees:
/// - try_push() never blocks (returns false if full)
/// - try_pop() never blocks (returns false if empty)
/// - No dynamic allocation
/// - Thread-safe via atomics
template<size_t Capacity>
class LockFreeQueue
{
public:
  LockFreeQueue() noexcept
  : head_(0), tail_(0)
  {
  }

  /// Try to push an event to the queue (non-blocking)
  /// Returns false if queue is full (graceful degradation)
  /// Thread-safe for multiple producers via CAS operation
  bool try_push(const TraceEvent & event) noexcept
  {
    size_t current_tail = tail_.load(std::memory_order_relaxed);

    // Retry loop with CAS to handle multiple producers
    for (int attempts = 0; attempts < 100; ++attempts) {
      const size_t next_tail = (current_tail + 1) % Capacity;
      const size_t current_head = head_.load(std::memory_order_acquire);

      // Check if queue is full
      if (next_tail == current_head) {
        return false;  // Queue full, drop event (safety guarantee)
      }

      // Try to atomically claim this slot via compare-and-swap
      if (tail_.compare_exchange_weak(
          current_tail,
          next_tail,
          std::memory_order_release,
          std::memory_order_relaxed))
      {
        // Successfully claimed slot at current_tail, write event
        buffer_[current_tail] = event;
        return true;
      }

      // CAS failed - another producer claimed this slot
      // current_tail now contains the updated value, retry
      std::this_thread::yield();
    }

    // Too much contention, give up
    return false;
  }

  /// Try to pop an event from the queue (non-blocking)
  /// Returns false if queue is empty
  bool try_pop(TraceEvent & event) noexcept
  {
    const size_t current_head = head_.load(std::memory_order_relaxed);
    const size_t current_tail = tail_.load(std::memory_order_acquire);

    // Check if queue is empty
    if (current_head == current_tail) {
      return false;  // Queue empty
    }

    // Read event from buffer
    event = buffer_[current_head];

    // Update head
    const size_t next_head = (current_head + 1) % Capacity;
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  /// Get approximate queue size (for monitoring)
  size_t size() const noexcept
  {
    const size_t current_head = head_.load(std::memory_order_relaxed);
    const size_t current_tail = tail_.load(std::memory_order_relaxed);

    if (current_tail >= current_head) {
      return current_tail - current_head;
    } else {
      return Capacity - current_head + current_tail;
    }
  }

  /// Check if queue is empty
  bool empty() const noexcept
  {
    return head_.load(std::memory_order_relaxed) ==
           tail_.load(std::memory_order_relaxed);
  }

  /// Get queue capacity
  constexpr size_t capacity() const noexcept
  {
    return Capacity;
  }

private:
  // Ring buffer storage (pre-allocated)
  TraceEvent buffer_[Capacity];

  // Atomic head and tail indices
  std::atomic<size_t> head_;  // Consumer reads from head
  std::atomic<size_t> tail_;  // Producer writes to tail
};

// Global trace event queue (default size: 1024 events)
// Configurable via ROBOTOPS_TRACE_QUEUE_SIZE env var
constexpr size_t DEFAULT_TRACE_QUEUE_SIZE = 1024;

/// Get the global trace event queue
/// Thread-safe singleton
LockFreeQueue<DEFAULT_TRACE_QUEUE_SIZE> & get_trace_event_queue() noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__TRACE_EVENT_QUEUE_HPP_
