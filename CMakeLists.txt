cmake_minimum_required(VERSION 3.8)
project(rmw_robotops)

# Compiler settings - strict mode
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# C++17 required for std::optional, std::string_view
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rcutils REQUIRED)
find_package(rmw REQUIRED)
find_package(rcpputils REQUIRED)
find_package(robotops_msgs REQUIRED)
find_package(rosidl_runtime_c REQUIRED)
find_package(rosidl_typesupport_introspection_c REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
find_package(rmw_fastrtps_cpp REQUIRED)
find_package(rmw_fastrtps_shared_cpp REQUIRED)
find_package(fastrtps REQUIRED)
find_package(fastcdr REQUIRED)

# Main library
add_library(${PROJECT_NAME} SHARED
  src/rmw_robotops.cpp
  src/rmw_init.cpp
  src/rmw_node.cpp
  src/rmw_publisher.cpp
  src/rmw_subscription.cpp
  src/rmw_service.cpp
  src/rmw_client.cpp
  src/trace_context.cpp
  src/trace_event_queue.cpp
  src/trace_publisher.cpp
  src/span_id_generator.cpp
  src/dds_metadata.cpp
  src/config.cpp
  src/rmw_stubs.cpp
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

ament_target_dependencies(${PROJECT_NAME}
  rcutils
  rmw
  rcpputils
  robotops_msgs
  rosidl_runtime_c
  rosidl_typesupport_introspection_c
  rosidl_typesupport_introspection_cpp
  rmw_fastrtps_cpp
  rmw_fastrtps_shared_cpp
  fastrtps
  fastcdr
)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} pthread)
endif()

# Export symbols
target_compile_definitions(${PROJECT_NAME}
  PRIVATE "RMW_ROBOTOPS_BUILDING_LIBRARY"
)

# Install library
install(
  TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# Install headers
install(
  DIRECTORY include/
  DESTINATION include
)

# Testing
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  # Linting
  ament_lint_auto_find_test_dependencies()

  # Unit tests
  ament_add_gtest(test_trace_context
    test/test_trace_context.cpp
  )
  target_link_libraries(test_trace_context ${PROJECT_NAME})
  ament_target_dependencies(test_trace_context rcutils)

  ament_add_gtest(test_span_id_generator
    test/test_span_id_generator.cpp
  )
  target_link_libraries(test_span_id_generator ${PROJECT_NAME})

  ament_add_gtest(test_lock_free_queue
    test/test_lock_free_queue.cpp
  )
  target_link_libraries(test_lock_free_queue ${PROJECT_NAME})

  # Safety tests (critical - must pass)
  ament_add_gtest(test_safety_guarantees
    test/test_safety_guarantees.cpp
    TIMEOUT 120
  )
  target_link_libraries(test_safety_guarantees ${PROJECT_NAME})
  ament_target_dependencies(test_safety_guarantees rmw rcutils)

  # Integration tests
  ament_add_gtest(test_context_propagation
    test/test_context_propagation.cpp
    TIMEOUT 60
  )
  target_link_libraries(test_context_propagation ${PROJECT_NAME})
  ament_target_dependencies(test_context_propagation rmw rcutils)

  # Metadata extraction test
  ament_add_gtest(test_metadata_extraction
    test/test_metadata_extraction.cpp
  )
  target_link_libraries(test_metadata_extraction ${PROJECT_NAME})

  # Performance benchmarks
  add_executable(benchmark_latency
    test/benchmark_latency.cpp
  )
  target_link_libraries(benchmark_latency ${PROJECT_NAME})
  ament_target_dependencies(benchmark_latency rmw rcutils)

  install(TARGETS benchmark_latency
    DESTINATION lib/${PROJECT_NAME}
  )
endif()

# Export package
ament_export_dependencies(
  rcutils
  rmw
  rcpputils
  robotops_msgs
  rosidl_runtime_c
  rmw_fastrtps_cpp
)

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})

ament_package()
