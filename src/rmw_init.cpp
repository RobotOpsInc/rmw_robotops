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

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

#include "rcutils/allocator.h"
#include "rmw/error_handling.h"
#include "rmw/init_options.h"
#include "rmw/rmw.h"
#include "rmw/security_options.h"
#include "rmw_robotops/config.hpp"
#include "rmw_robotops/trace_publisher.hpp"

// Function pointers to underlying RMW implementation
extern "C" {
// Publisher functions
rmw_ret_t (* underlying_rmw_publish)(
  const rmw_publisher_t *, const void *, rmw_publisher_allocation_t *) = nullptr;
rmw_ret_t (* underlying_rmw_publish_serialized_message)(
  const rmw_publisher_t *, const rmw_serialized_message_t *,
  rmw_publisher_allocation_t *) = nullptr;
rmw_publisher_t * (* underlying_rmw_create_publisher)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_publisher_options_t *) = nullptr;
rmw_ret_t (* underlying_rmw_destroy_publisher)(
  rmw_node_t *, rmw_publisher_t *) = nullptr;

// Subscription functions
rmw_subscription_t * (* underlying_rmw_create_subscription)(
  const rmw_node_t *, const rosidl_message_type_support_t *,
  const char *, const rmw_qos_profile_t *, const rmw_subscription_options_t *) = nullptr;
rmw_ret_t (* underlying_rmw_destroy_subscription)(
  rmw_node_t *, rmw_subscription_t *) = nullptr;
rmw_ret_t (* underlying_rmw_take)(
  const rmw_subscription_t *, void *, bool *, rmw_subscription_allocation_t *) = nullptr;
rmw_ret_t (* underlying_rmw_take_with_info)(
  const rmw_subscription_t *, void *, bool *,
  rmw_message_info_t *, rmw_subscription_allocation_t *) = nullptr;

// Client functions
rmw_client_t * (* underlying_rmw_create_client)(
  const rmw_node_t *, const rosidl_service_type_support_t *,
  const char *, const rmw_qos_profile_t *) = nullptr;
rmw_ret_t (* underlying_rmw_destroy_client)(
  rmw_node_t *, rmw_client_t *) = nullptr;
rmw_ret_t (* underlying_rmw_send_request)(
  const rmw_client_t *, const void *, int64_t *) = nullptr;
rmw_ret_t (* underlying_rmw_take_response)(
  const rmw_client_t *, rmw_service_info_t *, void *, bool *) = nullptr;

// Service functions
rmw_service_t * (* underlying_rmw_create_service)(
  const rmw_node_t *, const rosidl_service_type_support_t *,
  const char *, const rmw_qos_profile_t *) = nullptr;
rmw_ret_t (* underlying_rmw_destroy_service)(
  rmw_node_t *, rmw_service_t *) = nullptr;
rmw_ret_t (* underlying_rmw_take_request)(
  const rmw_service_t *, rmw_service_info_t *, void *, bool *) = nullptr;
rmw_ret_t (* underlying_rmw_send_response)(
  const rmw_service_t *, rmw_request_id_t *, void *) = nullptr;

// Node functions
rmw_node_t * (* underlying_rmw_create_node)(
  rmw_context_t *, const char *, const char *) = nullptr;
rmw_ret_t (* underlying_rmw_destroy_node)(rmw_node_t *) = nullptr;

// Initialization functions
rmw_ret_t (* underlying_rmw_init)(
  const rmw_init_options_t *, rmw_context_t *) = nullptr;
rmw_ret_t (* underlying_rmw_shutdown)(rmw_context_t *) = nullptr;

// Init options functions
rmw_init_options_t (* underlying_rmw_get_zero_initialized_init_options)(void) = nullptr;
rmw_ret_t (* underlying_rmw_init_options_init)(
  rmw_init_options_t *, rcutils_allocator_t) = nullptr;
rmw_ret_t (* underlying_rmw_init_options_copy)(
  const rmw_init_options_t *, rmw_init_options_t *) = nullptr;
rmw_ret_t (* underlying_rmw_init_options_fini)(rmw_init_options_t *) = nullptr;

// Exported for use by rmw_stubs.cpp
void * underlying_rmw_lib = nullptr;
}

/// Load function from underlying RMW library
template<typename FuncPtr>
static bool load_function(
  void * lib_handle,
  const char * func_name,
  FuncPtr & func_ptr)
{
  func_ptr = reinterpret_cast<FuncPtr>(dlsym(lib_handle, func_name));
  if (func_ptr == nullptr) {
    fprintf(
      stderr,
      "rmw_robotops: Failed to load function '%s': %s\n",
      func_name, dlerror());
    return false;
  }
  return true;
}

/// Load underlying RMW implementation dynamically
/// Exported for use by rmw_stubs.cpp
extern "C" bool load_underlying_rmw() noexcept
{
  const char * underlying_rmw_name = rmw_robotops::get_underlying_rmw();
  if (underlying_rmw_name == nullptr) {
    fprintf(stderr, "rmw_robotops: ROBOTOPS_UNDERLYING_RMW not set\n");
    return false;
  }

  // Construct library name: librmw_fastrtps_cpp.so, librmw_cyclonedds_cpp.so, etc.
  char lib_name[256];
  snprintf(lib_name, sizeof(lib_name), "lib%s.so", underlying_rmw_name);

  // Load library
  underlying_rmw_lib = dlopen(lib_name, RTLD_LAZY | RTLD_GLOBAL);
  if (underlying_rmw_lib == nullptr) {
    fprintf(
      stderr,
      "rmw_robotops: Failed to load underlying RMW '%s': %s\n",
      lib_name, dlerror());
    return false;
  }

  // Load required functions
  bool success = true;
  success &= load_function(underlying_rmw_lib, "rmw_publish", underlying_rmw_publish);
  success &= load_function(
    underlying_rmw_lib,
    "rmw_publish_serialized_message",
    underlying_rmw_publish_serialized_message);
  success &= load_function(
    underlying_rmw_lib, "rmw_create_publisher", underlying_rmw_create_publisher);
  success &= load_function(
    underlying_rmw_lib, "rmw_destroy_publisher", underlying_rmw_destroy_publisher);
  success &= load_function(
    underlying_rmw_lib, "rmw_create_subscription", underlying_rmw_create_subscription);
  success &= load_function(
    underlying_rmw_lib, "rmw_destroy_subscription", underlying_rmw_destroy_subscription);
  success &= load_function(underlying_rmw_lib, "rmw_take", underlying_rmw_take);
  success &= load_function(
    underlying_rmw_lib, "rmw_take_with_info", underlying_rmw_take_with_info);
  success &= load_function(
    underlying_rmw_lib, "rmw_create_client", underlying_rmw_create_client);
  success &= load_function(
    underlying_rmw_lib, "rmw_destroy_client", underlying_rmw_destroy_client);
  success &= load_function(underlying_rmw_lib, "rmw_send_request", underlying_rmw_send_request);
  success &= load_function(
    underlying_rmw_lib, "rmw_take_response", underlying_rmw_take_response);
  success &= load_function(
    underlying_rmw_lib, "rmw_create_service", underlying_rmw_create_service);
  success &= load_function(
    underlying_rmw_lib, "rmw_destroy_service", underlying_rmw_destroy_service);
  success &= load_function(underlying_rmw_lib, "rmw_take_request", underlying_rmw_take_request);
  success &= load_function(
    underlying_rmw_lib, "rmw_send_response", underlying_rmw_send_response);
  success &= load_function(underlying_rmw_lib, "rmw_create_node", underlying_rmw_create_node);
  success &= load_function(underlying_rmw_lib, "rmw_destroy_node", underlying_rmw_destroy_node);
  success &= load_function(underlying_rmw_lib, "rmw_init", underlying_rmw_init);
  success &= load_function(underlying_rmw_lib, "rmw_shutdown", underlying_rmw_shutdown);
  success &= load_function(
    underlying_rmw_lib,
    "rmw_get_zero_initialized_init_options",
    underlying_rmw_get_zero_initialized_init_options);
  success &= load_function(
    underlying_rmw_lib, "rmw_init_options_init", underlying_rmw_init_options_init);
  success &= load_function(
    underlying_rmw_lib, "rmw_init_options_copy", underlying_rmw_init_options_copy);
  success &= load_function(
    underlying_rmw_lib, "rmw_init_options_fini", underlying_rmw_init_options_fini);

  if (!success) {
    dlclose(underlying_rmw_lib);
    underlying_rmw_lib = nullptr;
    return false;
  }

  fprintf(stderr, "rmw_robotops: Loaded underlying RMW '%s'\n", lib_name);
  return true;
}

extern "C"
{
rmw_ret_t
rmw_init(const rmw_init_options_t * options, rmw_context_t * context)
{
  // Load underlying RMW on first initialization
  if (underlying_rmw_lib == nullptr) {
    if (!load_underlying_rmw()) {
      RMW_SET_ERROR_MSG("Failed to load underlying RMW implementation");
      return RMW_RET_ERROR;
    }
  }

  if (underlying_rmw_init == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW init function not loaded");
    return RMW_RET_ERROR;
  }

  // Delegate to underlying RMW
  rmw_ret_t ret = underlying_rmw_init(options, context);
  if (ret != RMW_RET_OK) {
    return ret;
  }

  // Start background trace publisher (only if tracing enabled)
  ret = rmw_robotops::start_trace_publisher(context);
  if (ret != RMW_RET_OK) {
    // Non-fatal: tracing failed but RMW is initialized
    // Log error but don't fail initialization
    fprintf(stderr, "rmw_robotops: Warning: Failed to start trace publisher\n");
  }

  return RMW_RET_OK;
}

rmw_ret_t
rmw_shutdown(rmw_context_t * context)
{
  // Stop background trace publisher first (drains remaining events)
  rmw_ret_t ret = rmw_robotops::stop_trace_publisher();
  if (ret != RMW_RET_OK) {
    fprintf(stderr, "rmw_robotops: Warning: Error stopping trace publisher\n");
  }

  // Then shutdown underlying RMW
  if (underlying_rmw_shutdown == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW shutdown function not loaded");
    return RMW_RET_ERROR;
  }

  return underlying_rmw_shutdown(context);
}

const char *
rmw_get_implementation_identifier()
{
  return "rmw_robotops";
}

rmw_init_options_t
rmw_get_zero_initialized_init_options()
{
  if (underlying_rmw_lib == nullptr) {
    load_underlying_rmw();
  }

  if (underlying_rmw_get_zero_initialized_init_options != nullptr) {
    return underlying_rmw_get_zero_initialized_init_options();
  }

  // Fallback if function not available
  rmw_init_options_t options;
  options.instance_id = 0;
  options.implementation_identifier = rmw_get_implementation_identifier();
  options.allocator = rcutils_get_default_allocator();
  options.impl = nullptr;
  options.enclave = nullptr;
  options.domain_id = RMW_DEFAULT_DOMAIN_ID;
  options.security_options = rmw_get_zero_initialized_security_options();
  options.localhost_only = RMW_LOCALHOST_ONLY_DEFAULT;
  return options;
}

rmw_ret_t
rmw_init_options_init(rmw_init_options_t * init_options, rcutils_allocator_t allocator)
{
  if (underlying_rmw_lib == nullptr) {
    if (!load_underlying_rmw()) {
      RMW_SET_ERROR_MSG("Failed to load underlying RMW implementation");
      return RMW_RET_ERROR;
    }
  }

  if (underlying_rmw_init_options_init == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW init_options_init function not loaded");
    return RMW_RET_ERROR;
  }

  return underlying_rmw_init_options_init(init_options, allocator);
}

rmw_ret_t
rmw_init_options_copy(const rmw_init_options_t * src, rmw_init_options_t * dst)
{
  if (underlying_rmw_lib == nullptr) {
    if (!load_underlying_rmw()) {
      RMW_SET_ERROR_MSG("Failed to load underlying RMW implementation");
      return RMW_RET_ERROR;
    }
  }

  if (underlying_rmw_init_options_copy == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW init_options_copy function not loaded");
    return RMW_RET_ERROR;
  }

  return underlying_rmw_init_options_copy(src, dst);
}

rmw_ret_t
rmw_init_options_fini(rmw_init_options_t * init_options)
{
  if (underlying_rmw_lib == nullptr) {
    // Already unloaded, nothing to do
    return RMW_RET_OK;
  }

  if (underlying_rmw_init_options_fini == nullptr) {
    RMW_SET_ERROR_MSG("Underlying RMW init_options_fini function not loaded");
    return RMW_RET_ERROR;
  }

  return underlying_rmw_init_options_fini(init_options);
}
}  // extern "C"
