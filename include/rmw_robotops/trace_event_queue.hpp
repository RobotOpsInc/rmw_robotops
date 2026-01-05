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

namespace rmw_robotops
{

/// Operation types for trace events
enum OperationType : uint8_t
{
  OP_PUBLISH = 0,
  OP_SUBSCRIBE = 1,
  OP_SERVICE_REQUEST = 2,
  OP_SERVICE_RESPONSE = 3,
  OP_CLIENT_REQUEST = 4,
  OP_CLIENT_RESPONSE = 5,
  OP_ACTION_GOAL_SENT = 6,
  OP_ACTION_CANCEL = 7,
  OP_ACTION_RESULT = 8,
  OP_ACTION_FEEDBACK = 9,
  OP_ACTION_STATUS = 10,
};

/// Maximum topic name length (pre-allocated, no dynamic allocation)
constexpr size_t MAX_TOPIC_NAME_LENGTH = 256;

/// Maximum number of span links per event (fan-in)
constexpr size_t MAX_SPAN_LINKS = 8;

/// Trace event emitted by RMW interception
/// Sent to background thread for publishing to /robotops/trace_events
struct TraceEvent
{
  // Trace context
  char trace_id[TRACE_ID_LENGTH + 1];
  char span_id[SPAN_ID_LENGTH + 1];
  char parent_span_id[SPAN_ID_LENGTH + 1];

  // Event metadata
  char topic_name[MAX_TOPIC_NAME_LENGTH];
  OperationType operation;
  uint64_t timestamp_ns;
  size_t message_size;

  // Span links (for fan-in)
  size_t span_link_count;
  TraceContext span_links[MAX_SPAN_LINKS];

  TraceEvent() noexcept
  : operation(OP_PUBLISH),
    timestamp_ns(0),
    message_size(0),
    span_link_count(0)
  {
    trace_id[0] = '\0';
    span_id[0] = '\0';
    parent_span_id[0] = '\0';
    topic_name[0] = '\0';
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
  bool try_push(const TraceEvent & event) noexcept
  {
    const size_t current_tail = tail_.load(std::memory_order_relaxed);
    const size_t next_tail = (current_tail + 1) % Capacity;
    const size_t current_head = head_.load(std::memory_order_acquire);

    // Check if queue is full
    if (next_tail == current_head) {
      return false;  // Queue full, drop event (safety guarantee)
    }

    // Write event to buffer
    buffer_[current_tail] = event;

    // Publish the write
    tail_.store(next_tail, std::memory_order_release);
    return true;
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
