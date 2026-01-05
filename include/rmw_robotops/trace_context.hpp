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

#ifndef RMW_ROBOTOPS__TRACE_CONTEXT_HPP_
#define RMW_ROBOTOPS__TRACE_CONTEXT_HPP_

#include <cstdint>
#include <cstring>

namespace rmw_robotops
{

// OpenTelemetry-compatible trace IDs (128-bit) and span IDs (64-bit)
// Represented as hex strings for compatibility
constexpr size_t TRACE_ID_LENGTH = 32;  // 128 bits as hex string
constexpr size_t SPAN_ID_LENGTH = 16;   // 64 bits as hex string

/// Trace context propagated through ROS2 message flow
/// Stored in thread-local storage for automatic propagation
struct TraceContext
{
  char trace_id[TRACE_ID_LENGTH + 1];      // +1 for null terminator
  char span_id[SPAN_ID_LENGTH + 1];        // Current span ID
  char parent_span_id[SPAN_ID_LENGTH + 1]; // Parent span ID (empty if root)

  /// Check if context is empty (no active trace)
  bool is_empty() const noexcept
  {
    return trace_id[0] == '\0';
  }

  /// Clear the context (make it empty)
  void clear() noexcept
  {
    trace_id[0] = '\0';
    span_id[0] = '\0';
    parent_span_id[0] = '\0';
  }

  /// Create an empty context
  static TraceContext empty() noexcept
  {
    TraceContext ctx;
    ctx.clear();
    return ctx;
  }
};

// Maximum number of pending contexts for fan-in (SpanLinks)
// Pre-allocated to avoid dynamic allocation in hot path
constexpr size_t MAX_PENDING_CONTEXTS = 16;

/// Storage for pending trace contexts (fan-in scenario)
/// When multiple messages arrive before we publish, we save their contexts as SpanLinks
struct PendingContexts
{
  TraceContext contexts[MAX_PENDING_CONTEXTS];
  size_t count;

  PendingContexts() noexcept : count(0) {}

  /// Add a context to pending list (for SpanLinks)
  /// Returns false if buffer is full (graceful degradation)
  bool add(const TraceContext & ctx) noexcept
  {
    if (count >= MAX_PENDING_CONTEXTS) {
      return false;  // Buffer full, silently drop (safety guarantee)
    }
    contexts[count++] = ctx;
    return true;
  }

  /// Clear all pending contexts
  void clear() noexcept
  {
    count = 0;
  }

  /// Check if empty
  bool is_empty() const noexcept
  {
    return count == 0;
  }
};

/// Get the current trace context for this thread
/// Returns an empty context if none is set
TraceContext get_trace_context() noexcept;

/// Set the trace context for this thread
/// Used when receiving a message with trace context
void set_trace_context(const TraceContext & ctx) noexcept;

/// Get or mint a new trace context
/// If current context is empty, generates a new trace_id (root span)
/// Otherwise returns the current context
TraceContext get_or_mint_trace_context() noexcept;

/// Save a context as pending (for fan-in/SpanLinks)
/// Returns false if pending buffer is full
bool save_pending_context(const TraceContext & ctx) noexcept;

/// Collect all pending contexts and clear the buffer
/// Used when creating a span to attach SpanLinks
/// Returns number of contexts collected
size_t collect_pending_contexts(TraceContext * out_contexts, size_t max_contexts) noexcept;

/// Clear all pending contexts
void clear_pending_contexts() noexcept;

}  // namespace rmw_robotops

#endif  // RMW_ROBOTOPS__TRACE_CONTEXT_HPP_
