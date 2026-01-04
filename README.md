# rmw_robotops

ROS2 RMW (ROS Middleware) implementation that wraps any underlying RMW (FastDDS, CycloneDDS, etc.) to add distributed tracing capabilities with OpenTelemetry-compatible context propagation.

## Overview

`rmw_robotops` is a **safety-critical** RMW layer that:

- Intercepts all ROS2 pub/sub/service/action communication
- Propagates trace context (trace_id, span_id) through DDS metadata
- Publishes trace events to `/robotops/trace_events` for collection
- Maintains 8 critical safety guarantees to ensure robot operation is never compromised

### Key Safety Guarantees

1. **Real message delivery is NEVER blocked** - Trace emission happens after real message is sent
2. **Best-Effort QoS** - Trace events never block the system
3. **No allocations in hot path** - Pre-allocated thread-local buffers
4. **No exceptions propagate** - All trace code is `noexcept`
5. **Lock-free context propagation** - Thread-local storage only
6. **Runtime kill switch** - Zero overhead when disabled
7. **Auto-disable on failures** - Self-protecting circuit breaker
8. **Background thread for publishing** - Non-blocking queue from robot threads

## Prerequisites

- ROS2 Jazzy
- Docker with buildx support
- Cloudsmith API key (for robotops_msgs)

## Setup

### 1. Configure Cloudsmith API Key

Create a file with your Cloudsmith API key:

```bash
mkdir -p ~/.cloudsmith
echo "YOUR_API_KEY_HERE" > ~/.cloudsmith/key
chmod 600 ~/.cloudsmith/key
```

### 2. Build with Docker Compose

```bash
# Build the development image
DOCKER_BUILDKIT=1 docker-compose build dev

# Or build all services
DOCKER_BUILDKIT=1 docker-compose build
```

### 3. Run Development Container

```bash
docker-compose run --rm dev
```

Inside the container:

```bash
# Build the package
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install

# Source the workspace
source install/setup.bash
```

## Usage

### Basic Configuration

```bash
# Required: Use rmw_robotops as the RMW implementation
export RMW_IMPLEMENTATION=rmw_robotops

# Required: Specify the underlying RMW to delegate to
export ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp

# Optional: Disable tracing (pure passthrough mode)
export ROBOTOPS_TRACING_ENABLED=false

# Optional: Topic filter (regex)
export ROBOTOPS_TRACE_TOPIC_FILTER="^/camera/.*|^/lidar/.*"
```

### Running with a ROS2 Node

```bash
# Terminal 1: Run your robot node with rmw_robotops
export RMW_IMPLEMENTATION=rmw_robotops
export ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp
ros2 run my_package my_node

# Terminal 2: Monitor trace events
ros2 topic echo /robotops/trace_events
```

## Testing

### Run All Tests

```bash
docker-compose run --rm test
```

### Run Specific Tests

```bash
# Inside dev container
colcon test --packages-select rmw_robotops
colcon test-result --verbose
```

### Safety Tests (Critical)

```bash
# These MUST pass before deploying to robots
colcon test --packages-select rmw_robotops --ctest-args -R test_safety
```

### Performance Benchmarks

```bash
# Inside dev container
./install/rmw_robotops/lib/rmw_robotops/benchmark_latency
```

**Requirements:**
- Median latency: < 1µs added overhead
- CPU overhead: < 5% vs underlying RMW
- Memory: Zero allocations in hot path

### Stress Test

```bash
docker-compose run --rm stress
```

Tests 10,000 msg/sec with bounded memory usage.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Robot Node Process                       │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐   │
│   │              rmw_robotops                           │   │
│   │                                                     │   │
│   │   rmw_publish() → [Real Message First]            │   │
│   │                 → [Best-Effort Trace Event]        │   │
│   │                                                     │   │
│   │   rmw_take()    → [Extract Context from DDS]       │   │
│   │                 → [Set Thread-Local Context]       │   │
│   │                                                     │   │
│   │   Background Thread → [/robotops/trace_events]     │   │
│   └─────────────────────────────────────────────────────┘   │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐   │
│   │         Underlying RMW (FastDDS/CycloneDDS)         │   │
│   └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Package Structure

```
rmw_robotops/
├── CMakeLists.txt           # ament_cmake build config
├── package.xml              # ROS2 package manifest
├── Dockerfile               # Development image
├── Dockerfile.test          # Testing image with sanitizers
├── docker-compose.yml       # Multi-service orchestration
├── src/
│   ├── rmw_robotops.cpp         # Main RMW interface
│   ├── rmw_init.cpp             # Initialization/shutdown
│   ├── rmw_node.cpp             # Node lifecycle
│   ├── rmw_publisher.cpp        # Publish interception
│   ├── rmw_subscription.cpp     # Subscribe interception
│   ├── rmw_service.cpp          # Service server interception
│   ├── rmw_client.cpp           # Service client interception
│   ├── trace_context.cpp        # Thread-local context management
│   ├── trace_event_queue.cpp    # Lock-free bounded queue
│   ├── trace_publisher.cpp      # Background publisher thread
│   ├── span_id_generator.cpp    # Fast PRNG for IDs
│   ├── dds_metadata.cpp         # Context injection/extraction
│   └── config.cpp               # Environment variable parsing
├── include/rmw_robotops/
│   └── visibility.h             # Export macros
└── test/
    ├── test_safety_guarantees.cpp
    ├── test_trace_context.cpp
    ├── test_span_id_generator.cpp
    ├── test_lock_free_queue.cpp
    ├── test_context_propagation.cpp
    └── benchmark_latency.cpp
```

## Development Workflow

### Local Development (Mac)

All development happens in Docker containers since ROS2 Jazzy doesn't run natively on macOS.

```bash
# Start dev container
docker-compose run --rm dev

# Inside container: make changes, rebuild
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Debug

# Run tests
colcon test --event-handlers console_direct+
```

### Adding New Tests

1. Create test file in `test/`
2. Add to `CMakeLists.txt` using `ament_add_gtest()`
3. Rebuild and run:
   ```bash
   colcon build --packages-select rmw_robotops
   colcon test --packages-select rmw_robotops
   ```

## Troubleshooting

### Cloudsmith Authentication Failed

```bash
# Verify your API key is correct
cat ~/.cloudsmith/key

# Rebuild without cache
docker-compose build --no-cache dev
```

### Build Fails with Missing robotops_msgs

The package depends on `robotops_msgs` from Cloudsmith. Ensure:
1. API key is configured correctly
2. You're using `DOCKER_BUILDKIT=1` for secret support
3. The setup script ran successfully (check Docker build logs)

### Tests Fail with AddressSanitizer

This is expected during development. Safety tests with ASan are designed to catch memory issues early.

```bash
# Run with detailed sanitizer output
docker-compose run --rm test
```

## Related Issues

- **ROB-55**: This implementation (rmw_robotops)
- **ROB-54**: robotops_msgs package (TraceEvent.msg)
- **ROB-56**: Robot Agent span buffer and export
- **ROB-33**: Distributed Tracing epic (parent)
- **ROB-105**: Multi-DDS testing (CycloneDDS, etc.)

## License

Apache-2.0