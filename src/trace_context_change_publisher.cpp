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

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw_robotops/config.hpp"
#include "rmw_robotops/trace_context_change_publisher.hpp"
#include "robotops_msgs/msg/trace_context_change.h"
#include "rosidl_runtime_c/string_functions.h"

// Forward declarations of underlying RMW functions (defined in rmw_init.cpp)
extern "C" {
extern rmw_node_t * (* underlying_rmw_create_node)(
  rmw_context_t *, const char *, const char *);
extern rmw_ret_t (* underlying_rmw_destroy_node)(rmw_node_t *);
extern rmw_publisher_t * (* underlying_rmw_create_publisher)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_publisher_options_t *);
extern rmw_ret_t (* underlying_rmw_destroy_publisher)(
  rmw_node_t *, rmw_publisher_t *);
extern rmw_ret_t (* underlying_rmw_publish)(
  const rmw_publisher_t *, const void *, rmw_publisher_allocation_t *);
}

namespace rmw_robotops
{

namespace
{

/// MPSC lock-free ring buffer for TraceContextChangeEvent
/// Capacity must be a power of two for correct ring arithmetic
template<size_t Capacity>
class ContextChangeQueue
{
public:
  ContextChangeQueue() noexcept
  : head_(0), tail_(0) {}

  /// Try to push (non-blocking). Returns false if full.
  bool try_push(const TraceContextChangeEvent & event) noexcept
  {
    size_t current_tail = tail_.load(std::memory_order_relaxed);

    for (int attempts = 0; attempts < 100; ++attempts) {
      const size_t next_tail = (current_tail + 1) % Capacity;
      const size_t current_head = head_.load(std::memory_order_acquire);

      if (next_tail == current_head) {
        return false;  // Queue full
      }

      if (tail_.compare_exchange_weak(
          current_tail,
          next_tail,
          std::memory_order_release,
          std::memory_order_relaxed))
      {
        buffer_[current_tail] = event;
        return true;
      }

      std::this_thread::yield();
    }

    return false;  // Too much contention
  }

  /// Try to pop (non-blocking, single-consumer only). Returns false if empty.
  bool try_pop(TraceContextChangeEvent & event) noexcept
  {
    const size_t current_head = head_.load(std::memory_order_relaxed);
    const size_t current_tail = tail_.load(std::memory_order_acquire);

    if (current_head == current_tail) {
      return false;  // Queue empty
    }

    event = buffer_[current_head];
    head_.store((current_head + 1) % Capacity, std::memory_order_release);
    return true;
  }

private:
  TraceContextChangeEvent buffer_[Capacity];
  std::atomic<size_t> head_;
  std::atomic<size_t> tail_;
};

constexpr size_t CONTEXT_CHANGE_QUEUE_SIZE = 512;

// Background publisher state
std::atomic<bool> context_change_running{false};
std::thread context_change_thread;
rmw_node_t * context_change_node = nullptr;
rmw_publisher_t * context_change_publisher = nullptr;
ContextChangeQueue<CONTEXT_CHANGE_QUEUE_SIZE> context_change_queue;

/// Convert internal event to ROS2 C message.
/// Memory allocation is fine here — this runs in the background thread.
bool convert_to_ros_message(
  const TraceContextChangeEvent & event,
  robotops_msgs__msg__TraceContextChange & msg) noexcept
{
  try {
    msg.timestamp.sec = static_cast<int32_t>(event.timestamp_ns / 1000000000ULL);
    msg.timestamp.nanosec = static_cast<uint32_t>(event.timestamp_ns % 1000000000ULL);
    rosidl_runtime_c__String__assign(&msg.node_name, event.node_name);
    rosidl_runtime_c__String__assign(&msg.node_namespace, event.node_namespace);
    msg.thread_id = event.thread_id;
    rosidl_runtime_c__String__assign(&msg.trace_id, event.trace_id);
    rosidl_runtime_c__String__assign(&msg.span_id, event.span_id);
    msg.change_type = event.change_type;
    return true;
  } catch (...) {
    return false;
  }
}

/// Background thread: drains the queue and publishes to /robotops/trace_context
void context_change_thread_func() noexcept
{
  robotops_msgs__msg__TraceContextChange msg;
  robotops_msgs__msg__TraceContextChange__init(&msg);

  while (context_change_running.load(std::memory_order_relaxed)) {
    TraceContextChangeEvent event;
    if (context_change_queue.try_pop(event)) {
      if (convert_to_ros_message(event, msg)) {
        if (context_change_publisher != nullptr && underlying_rmw_publish != nullptr) {
          underlying_rmw_publish(context_change_publisher, &msg, nullptr);
        }
        robotops_msgs__msg__TraceContextChange__fini(&msg);
        robotops_msgs__msg__TraceContextChange__init(&msg);
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Drain remaining events before shutdown
  TraceContextChangeEvent event;
  while (context_change_queue.try_pop(event)) {
    if (convert_to_ros_message(event, msg)) {
      if (context_change_publisher != nullptr && underlying_rmw_publish != nullptr) {
        underlying_rmw_publish(context_change_publisher, &msg, nullptr);
      }
      robotops_msgs__msg__TraceContextChange__fini(&msg);
      robotops_msgs__msg__TraceContextChange__init(&msg);
    }
  }

  robotops_msgs__msg__TraceContextChange__fini(&msg);
}

}  // anonymous namespace

bool enqueue_context_change(const TraceContextChangeEvent & event) noexcept
{
  return context_change_queue.try_push(event);
}

rmw_ret_t start_trace_context_change_publisher(rmw_context_t * context) noexcept
{
  if (context_change_running.load(std::memory_order_relaxed)) {
    return RMW_RET_OK;  // Idempotent
  }

  if (!is_tracing_enabled()) {
    return RMW_RET_OK;  // No-op when tracing disabled
  }

  if (underlying_rmw_create_node == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  context_change_node = underlying_rmw_create_node(
    context, "rmw_robotops_context_change", "/robotops");
  if (context_change_node == nullptr) {
    RMW_SET_ERROR_MSG("Failed to create trace context change publisher node");
    return RMW_RET_ERROR;
  }

  const rosidl_message_type_support_t * type_support =
    ROSIDL_GET_MSG_TYPE_SUPPORT(robotops_msgs, msg, TraceContextChange);
  if (type_support == nullptr) {
    underlying_rmw_destroy_node(context_change_node);
    context_change_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to get TraceContextChange type support");
    return RMW_RET_ERROR;
  }

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 50;

  rmw_publisher_options_t pub_options = rmw_get_default_publisher_options();

  if (underlying_rmw_create_publisher == nullptr) {
    underlying_rmw_destroy_node(context_change_node);
    context_change_node = nullptr;
    RMW_SET_ERROR_MSG("Underlying RMW create_publisher not available");
    return RMW_RET_ERROR;
  }

  context_change_publisher = underlying_rmw_create_publisher(
    context_change_node, type_support, "/robotops/trace_context", &qos, &pub_options);

  if (context_change_publisher == nullptr) {
    underlying_rmw_destroy_node(context_change_node);
    context_change_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to create trace context change publisher");
    return RMW_RET_ERROR;
  }

  context_change_running.store(true, std::memory_order_relaxed);
  try {
    context_change_thread = std::thread(context_change_thread_func);
  } catch (...) {
    context_change_running.store(false, std::memory_order_relaxed);
    underlying_rmw_destroy_publisher(context_change_node, context_change_publisher);
    underlying_rmw_destroy_node(context_change_node);
    context_change_publisher = nullptr;
    context_change_node = nullptr;
    RMW_SET_ERROR_MSG("Failed to start trace context change publisher thread");
    return RMW_RET_ERROR;
  }

  return RMW_RET_OK;
}

rmw_ret_t stop_trace_context_change_publisher() noexcept
{
  if (!context_change_running.load(std::memory_order_relaxed)) {
    return RMW_RET_OK;  // Idempotent
  }

  context_change_running.store(false, std::memory_order_relaxed);

  if (context_change_thread.joinable()) {
    try {
      context_change_thread.join();
    } catch (...) {
      // Safety guarantee: Never propagate exceptions
    }
  }

  if (context_change_publisher != nullptr && underlying_rmw_destroy_publisher != nullptr) {
    underlying_rmw_destroy_publisher(context_change_node, context_change_publisher);
    context_change_publisher = nullptr;
  }

  if (context_change_node != nullptr && underlying_rmw_destroy_node != nullptr) {
    underlying_rmw_destroy_node(context_change_node);
    context_change_node = nullptr;
  }

  return RMW_RET_OK;
}

}  // namespace rmw_robotops
