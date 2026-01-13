# Compatibility Matrix

This document tracks tested and supported versions of dependencies for rmw_robotops.

## Design Philosophy

**rmw_robotops is DDS-agnostic.** It works with any DDS implementation (FastDDS, CycloneDDS, Connext, etc.) by using only standard RMW APIs. Trace correlation uses content hashing via introspection, not DDS-specific features.

## ROS2 Jazzy Jalisco (Ubuntu 24.04)

| Dependency | Supported Versions | Breaking At | Notes |
|------------|-------------------|-------------|-------|
| FastDDS | 2.x (2.6 - 2.14), 3.x | - | Works with any version (DDS-agnostic) |
| rmw_fastrtps_cpp | 8.x | 9.x | Default DDS on Jazzy |
| CycloneDDS | 0.10.x | - | Fully supported (DDS-agnostic design) |
| Connext DDS | Any | - | Fully supported (DDS-agnostic design) |
| robotops_msgs | >= 0.3.2 | - | Trace event message definitions |
| robotops-config | >= 0.4.14 | - | Configuration management |

### DDS Implementation Notes

rmw_robotops uses **content-based correlation** via message introspection:
- Publisher GID (24-byte unique identifier)
- Source timestamp (nanosecond precision)
- Content hash (FNV-1a hash of full message structure)

This approach works identically across all DDS implementations. No DDS-specific features required.

### Testing Notes

- CI runs against ROS2 Jazzy with FastDDS 2.14.x (default)
- Additional DDS implementations tested manually
- Cross-DDS compatibility (e.g., FastDDS publisher → CycloneDDS subscriber) works correctly

## Future: ROS2 Kilted (Ubuntu 26.04)

Expected to work without changes due to DDS-agnostic design. Will verify:
- Compatibility with default DDS implementation
- Testing against Kilted packages
- Performance validation

## References

- [ROS2 Distributions](https://docs.ros.org/en/rolling/Releases.html)
- [RMW Interface](https://design.ros2.org/articles/ros_middleware_interface.html)
- [Message Introspection API](https://design.ros2.org/articles/rosidl_runtime_c.html)
