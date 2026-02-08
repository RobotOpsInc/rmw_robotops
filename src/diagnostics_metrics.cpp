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

#include "rmw_robotops/diagnostics_metrics.hpp"

namespace rmw_robotops
{

DiagnosticsMetrics & get_diagnostics_metrics() noexcept
{
  // Thread-safe static initialization (C++11 guarantee)
  static DiagnosticsMetrics metrics;
  return metrics;
}

}  // namespace rmw_robotops
