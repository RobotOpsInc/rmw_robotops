#!/usr/bin/env python3
# Copyright 2025 RobotOps
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Generate RMW pass-through wrapper functions for rmw_robotops.

This script creates wrapper implementations for all RMW API functions
by examining the underlying RMW library and generating pass-through code.
"""

import re
import subprocess

# Functions we've already implemented (don't regenerate)
IMPLEMENTED_FUNCTIONS = {
    'rmw_create_node',
    'rmw_destroy_node',
    'rmw_create_publisher',
    'rmw_destroy_publisher',
    'rmw_publish',
    'rmw_publish_serialized_message',
    'rmw_create_subscription',
    'rmw_destroy_subscription',
    'rmw_take',
    'rmw_take_with_info',
    'rmw_init',
    'rmw_shutdown',
    'rmw_get_implementation_identifier',
    'rmw_init_options_init',
    'rmw_init_options_copy',
    'rmw_init_options_fini',
    'rmw_get_zero_initialized_init_options',
}


def get_rmw_functions():
    """Get list of RMW functions from FastDDS library."""
    result = subprocess.run(
        ['nm', '-D', '/opt/ros/jazzy/lib/librmw_fastrtps_cpp.so'],
        capture_output=True,
        text=True
    )

    functions = []
    for line in result.stdout.split('\n'):
        match = re.search(r'T (rmw_\w+)', line)
        if match:
            func_name = match.group(1)
            if func_name not in IMPLEMENTED_FUNCTIONS:
                functions.append(func_name)

    return sorted(functions)


def generate_function_pointer_decl(func_name):
    """Generate extern function pointer declaration."""
    return (f'// Auto-generated pass-through for {func_name}\n'
            f'void * (* underlying_{func_name})() = nullptr;')


def generate_function_wrapper(func_name):
    """Generate wrapper function implementation."""
    return f'''
// Auto-generated wrapper for {func_name}
extern "C" __attribute__((weak))
void * {func_name}()
{{
  if (underlying_{func_name} == nullptr) {{
    if (underlying_rmw_lib != nullptr) {{
      underlying_{func_name} = reinterpret_cast<void *(*)()>(
        dlsym(underlying_rmw_lib, '{func_name}'));
    }}
    if (underlying_{func_name} == nullptr) {{
      fprintf(stderr, 'rmw_robotops: {func_name} not available in underlying RMW\\n');
      return nullptr;
    }}
  }}
  return underlying_{func_name}();
}}
'''


def main():
    print('Generating RMW pass-through functions...')

    functions = get_rmw_functions()
    print(f'Found {len(functions)} functions to wrap')

    # Generate function pointer declarations
    with open('rmw_stubs_declarations.txt', 'w') as f:
        f.write('// Auto-generated function pointer declarations\n')
        f.write('// Add these to rmw_init.cpp in the extern "C" block\n\n')
        for func in functions:
            f.write(generate_function_pointer_decl(func) + '\n')

    # Generate wrapper implementations
    with open('rmw_stubs_implementations.txt', 'w') as f:
        f.write('// Auto-generated wrapper function implementations\n')
        f.write('// Add these to a new file: src/rmw_stubs.cpp\n\n')
        f.write('#include "rmw/rmw.h"\n')
        f.write('#include <dlfcn.h>\n')
        f.write('#include <cstdio>\n\n')
        f.write('extern "C" {\n')
        f.write('extern void * underlying_rmw_lib;\n')
        for func in functions:
            f.write(generate_function_wrapper(func))
        f.write('}\n')

    print('Generated code in rmw_stubs_declarations.txt and rmw_stubs_implementations.txt')
    print('\nNext steps:')
    print('1. Review generated code')
    print('2. Add declarations to rmw_init.cpp')
    print('3. Create src/rmw_stubs.cpp with implementations')
    print('4. Add rmw_stubs.cpp to CMakeLists.txt')


if __name__ == '__main__':
    main()
