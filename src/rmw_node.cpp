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

#include "rmw/rmw.h"
#include "rmw/error_handling.h"

// Forward declaration of underlying RMW functions
extern "C" {
extern rmw_node_t * (* underlying_rmw_create_node)(
  rmw_context_t *, const char *, const char *);
extern rmw_ret_t (* underlying_rmw_destroy_node)(rmw_node_t *);
}

extern "C"
{

rmw_node_t *
rmw_create_node(
  rmw_context_t * context,
  const char * name,
  const char * namespace_)
{
  // Delegate to underlying RMW - no interception needed for node creation
  if (underlying_rmw_create_node == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return nullptr;
  }

  return underlying_rmw_create_node(context, name, namespace_);
}

rmw_ret_t
rmw_destroy_node(rmw_node_t * node)
{
  // Delegate to underlying RMW - no interception needed for node destruction
  if (underlying_rmw_destroy_node == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW not initialized");
    return RMW_RET_ERROR;
  }

  return underlying_rmw_destroy_node(node);
}

}  // extern "C"
