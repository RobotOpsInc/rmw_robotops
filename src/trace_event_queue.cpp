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

#include "rmw_robotops/trace_event_queue.hpp"

namespace rmw_robotops
{

// Global trace event queue (static singleton)
static LockFreeQueue<DEFAULT_TRACE_QUEUE_SIZE> g_trace_event_queue;

LockFreeQueue<DEFAULT_TRACE_QUEUE_SIZE> & get_trace_event_queue() noexcept
{
  return g_trace_event_queue;
}

}  // namespace rmw_robotops
