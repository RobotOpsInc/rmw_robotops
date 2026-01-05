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

#include "rmw_robotops/trace_context.hpp"
#include "rmw_robotops/span_id_generator.hpp"

#include <cstring>

namespace rmw_robotops
{

// Thread-local storage for trace context (lock-free, safety guarantee #5)
thread_local TraceContext tls_trace_context = TraceContext::empty();

// Thread-local storage for pending contexts (fan-in)
thread_local PendingContexts tls_pending_contexts;

TraceContext get_trace_context() noexcept
{
  return tls_trace_context;
}

void set_trace_context(const TraceContext & ctx) noexcept
{
  tls_trace_context = ctx;
}

TraceContext get_or_mint_trace_context() noexcept
{
  // If we already have a trace context, return it
  if (!tls_trace_context.is_empty()) {
    return tls_trace_context;
  }

  // No incoming context - mint a new trace_id (this is a root span)
  // This happens for sensor publishes, timers, etc.
  TraceContext new_ctx;

  // Generate new trace_id (128-bit)
  generate_trace_id(new_ctx.trace_id);

  // Generate new span_id (64-bit)
  generate_span_id(new_ctx.span_id);

  // Root span has no parent
  new_ctx.parent_span_id[0] = '\0';

  // Set as current context
  tls_trace_context = new_ctx;

  return new_ctx;
}

bool save_pending_context(const TraceContext & ctx) noexcept
{
  return tls_pending_contexts.add(ctx);
}

size_t collect_pending_contexts(TraceContext * out_contexts, size_t max_contexts) noexcept
{
  if (out_contexts == nullptr || max_contexts == 0) {
    return 0;
  }

  size_t count = tls_pending_contexts.count;
  if (count > max_contexts) {
    count = max_contexts;  // Truncate if output buffer too small
  }

  // Copy pending contexts to output
  for (size_t i = 0; i < count; ++i) {
    out_contexts[i] = tls_pending_contexts.contexts[i];
  }

  // Clear pending contexts after collection
  tls_pending_contexts.clear();

  return count;
}

void clear_pending_contexts() noexcept
{
  tls_pending_contexts.clear();
}

}  // namespace rmw_robotops
