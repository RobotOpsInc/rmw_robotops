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

#ifndef RMW_ROBOTOPS__VISIBILITY_CONTROL_HPP_
#define RMW_ROBOTOPS__VISIBILITY_CONTROL_HPP_

#ifdef __cplusplus
extern "C" {
#endif

// This logic was borrowed (then namespaced) from the examples on the gcc wiki:
//     https://gcc.gnu.org/wiki/Visibility

#if defined _WIN32 || defined __CYGWIN__
  #ifdef RMW_ROBOTOPS_BUILDING_LIBRARY
    #ifdef __GNUC__
      #define RMW_ROBOTOPS_PUBLIC __attribute__((dllexport))
    #else
      #define RMW_ROBOTOPS_PUBLIC __declspec(dllexport)
    #endif
  #else
    #ifdef __GNUC__
      #define RMW_ROBOTOPS_PUBLIC __attribute__((dllimport))
    #else
      #define RMW_ROBOTOPS_PUBLIC __declspec(dllimport)
    #endif
  #endif
  #define RMW_ROBOTOPS_LOCAL
#else
  #define RMW_ROBOTOPS_PUBLIC __attribute__((visibility("default")))
  #define RMW_ROBOTOPS_LOCAL __attribute__((visibility("hidden")))
#endif

#ifdef __cplusplus
}
#endif

#endif  // RMW_ROBOTOPS__VISIBILITY_CONTROL_HPP_
