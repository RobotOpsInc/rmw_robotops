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

"""Extract RMW function signatures from rmw.h and generate stub implementations."""

import re


# Functions we've already implemented
IMPLEMENTED = {
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


def extract_function_signature(lines, start_idx):
    """Extract a complete function signature starting from RMW_PUBLIC."""
    sig_lines = []
    i = start_idx

    # Skip RMW_PUBLIC line
    while i < len(lines) and 'RMW_PUBLIC' in lines[i]:
        i += 1

    # Collect lines until we hit a semicolon
    paren_count = 0
    while i < len(lines):
        line = lines[i].strip()
        sig_lines.append(line)

        paren_count += line.count('(') - line.count(')')

        if ';' in line and paren_count == 0:
            break
        i += 1

    return ' '.join(sig_lines), i + 1


def parse_rmw_header(header_path):
    """Parse rmw.h and extract function signatures."""
    with open(header_path, 'r') as f:
        lines = f.readlines()

    functions = []
    i = 0

    while i < len(lines):
        line = lines[i]

        # Skip commented-out lines
        if line.strip().startswith('//'):
            i += 1
            continue

        # Look for RMW_PUBLIC declarations
        if 'RMW_PUBLIC' in line and i + 1 < len(lines):
            sig, next_i = extract_function_signature(lines, i)

            # Extract function name
            match = re.search(r'(rmw_[a-z_]+)\s*\(', sig)
            if match:
                func_name = match.group(1)
                if func_name not in IMPLEMENTED:
                    functions.append((func_name, sig))

            i = next_i
        else:
            i += 1

    return functions


def generate_stub(func_name, signature):
    """Generate a stub implementation that forwards to underlying RMW."""
    # Clean up the signature - remove various macros and attributes
    sig = signature.replace('RMW_PUBLIC', '').strip()
    sig = re.sub(r'RMW_WARN_UNUSED\s+', '', sig)
    sig = re.sub(r'RCUTILS_DEPRECATED_WITH_MSG\([^)]*\)\s+', '', sig)
    sig = re.sub(r'RCUTILS_DEPRECATED\s+', '', sig)
    sig = re.sub(r'__attribute__\(\([^)]*\)\)\s+', '', sig)

    # Extract return type and parameters
    match = re.match(r'(.*?)(rmw_[a-z_]+)\s*\((.*?)\);', sig, re.DOTALL)
    if not match:
        return f'// Could not parse: {func_name}\n'

    return_type = match.group(1).strip()
    params = match.group(3).strip()

    # Generate parameter names for the call
    param_list = []
    if params and params != 'void':
        for param in params.split(','):
            # Extract parameter name (last word before optional array brackets)
            param = param.strip()
            if param:
                # Remove array brackets and get last identifier
                param_clean = re.sub(r'\[.*?\]', '', param)
                tokens = param_clean.split()
                if tokens:
                    # Get the last token (parameter name)
                    param_name = tokens[-1].strip('*')
                    param_list.append(param_name)

    param_call = ', '.join(param_list)

    # Generate the stub
    code = f"""
{return_type}
{func_name}({params})
{{
  // Load underlying RMW if not already loaded
  if (underlying_rmw_lib == nullptr) {{
    load_underlying_rmw();
  }}

  // Load function pointer if needed
  static auto underlying_func = reinterpret_cast<{return_type}(*)({params})>(
    dlsym(underlying_rmw_lib, "{func_name}"));

  if (underlying_func == nullptr) {{
    fprintf(stderr, "rmw_robotops: {func_name} not available\\n");
"""

    # Return appropriate default based on return type
    return_type_clean = return_type.strip()
    if return_type_clean == 'void' or return_type_clean == '':
        # For void functions, just return without a value
        code += '    return;\n'
    elif 'rmw_ret_t' in return_type:
        code += '    return RMW_RET_ERROR;\n'
    elif '*' in return_type:
        code += '    return nullptr;\n'
    elif 'bool' in return_type:
        code += '    return false;\n'
    else:
        code += '    return {};\n'

    code += '  }\n  \n'

    # Add return statement if needed
    return_type_clean = return_type.strip()
    if return_type_clean == 'void' or return_type_clean == '':
        # For void functions, just call without return
        code += f'  underlying_func({param_call});\n'
    else:
        # For non-void functions, return the result
        code += f'  return underlying_func({param_call});\n'

    code += '}\n'

    return code


def main():
    import glob

    # Get all RMW header files
    header_paths = glob.glob('/opt/ros/jazzy/include/rmw/rmw/*.h')

    print(f'Parsing {len(header_paths)} header files...')

    all_functions = {}
    for header_path in sorted(header_paths):
        functions = parse_rmw_header(header_path)
        for func_name, sig in functions:
            if func_name not in all_functions:
                all_functions[func_name] = sig

    functions = list(all_functions.items())
    print(f'Found {len(functions)} unique functions to implement')

    # Generate stub file
    with open('rmw_stubs_generated.cpp', 'w') as f:
        f.write("""// Auto-generated RMW stub implementations
// Generated by extract_rmw_signatures.py

#include "rmw/rmw.h"
#include "rmw/error_handling.h"
#include "rmw/types.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/security_options.h"
#include "rmw/event.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/names_and_types.h"
#include "rmw/topic_endpoint_info_array.h"
#include "rmw/network_flow_endpoint_array.h"
#include "rmw/features.h"
#include "rmw/dynamic_message_type_support.h"

#include <dlfcn.h>
#include <cstdio>

// External declarations
extern "C" {
extern void * underlying_rmw_lib;
extern bool load_underlying_rmw();
}

extern "C" {

""")

        for func_name, sig in functions:
            stub = generate_stub(func_name, sig)
            f.write(stub)

        f.write("\n}  // extern \"C\"\n")

    print(f'Generated rmw_stubs_generated.cpp with {len(functions)} functions')

    # Also generate a list of function names
    with open('rmw_functions_list.txt', 'w') as f:
        for func_name, _ in functions:
            f.write(f"{func_name}\n")

    print('Generated rmw_functions_list.txt')


if __name__ == '__main__':
    main()
