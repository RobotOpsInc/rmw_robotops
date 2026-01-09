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

## Installation

### Via apt (Recommended for End Users)

```bash
# Add Cloudsmith repository (one-time setup)
curl -1sLf 'https://dl.cloudsmith.io/public/robotops/robotops/setup.deb.sh' | sudo bash

# Install
sudo apt update
sudo apt install ros-jazzy-rmw-robotops
```

### From Source (For Development)

See the Development section below for building from source in Docker.

## Prerequisites (Development Only)

**Batteries included!** Only 2 things needed on your host machine:

1. **Docker** with buildx support (for secrets)
   - macOS: Docker Desktop (buildx included by default)
   - Linux: `docker buildx install`
2. **Cloudsmith API key** (for private `robotops_msgs` dependency)

Everything else runs in containers - no ROS2, no Ubuntu required on host!

## Quick Start

### 1. Install just (task runner)

```bash
# macOS
brew install just

# Linux
curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to ~/bin
```

### 2. Configure Cloudsmith Credentials

Create a `.env.local` file in the project root with your Cloudsmith credentials:

```bash
# Copy the template
cp .env.local.template .env.local

# Edit with your credentials
# CLOUDSMITH_USERNAME=your-cloudsmith-username
# CLOUDSMITH_API_KEY=your-cloudsmith-api-key
```

The `.env.local` file is automatically loaded by `docker-compose` and `.gitignore`'d to keep credentials secure.

### 3. Build and develop

```bash
# See all available commands
just

# Build development image (uses defaults: robotops-development repo, v0.1.6)
just build

# Build with different Cloudsmith repo or version
just build robotops-production 0.2.0-0noble

# Or set via environment variables
export CLOUDSMITH_REPO=robotops-production
export ROBOTOPS_MSGS_VERSION=0.2.0-0noble
just build

# Start interactive shell
just dev

# Inside container, build the package:
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
```

### Advanced: Cloudsmith Configuration

The Docker build can pull `robotops_msgs` from different Cloudsmith repositories and versions:

**Via Justfile parameters:**
```bash
just build <repo> <version>
just build robotops-production 0.2.0-0noble
```

**Via environment variables:**
```bash
export CLOUDSMITH_REPO=robotops-staging
export ROBOTOPS_MSGS_VERSION=0.1.8-0noble
just build
```

**Defaults:**
- Repository: `robotops-development`
- Version: `0.1.6-0noble`

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

## Common Tasks

All tasks use `just` commands for simplicity. Run `just` to see all available commands.

### Development 

```bash
just dev          # Interactive development shell
just compile      # Build the package
just clean        # Remove build artifacts
just rebuild      # Clean + rebuild from scratch
```

### Testing

```bash
just test         # Run all tests with sanitizers
just test-safety  # Run safety tests (MUST pass before deployment)
just benchmark    # Performance benchmarks
just stress       # Stress test (10,000 msg/sec)
just logs         # Show logs from last test run
```

**Performance Requirements:**
- Median latency: < 1µs added overhead
- CPU overhead: < 5% vs underlying RMW
- Memory: Zero allocations in hot path

### CI/CD

**Reproducing GitHub Actions CI locally:**

```bash
# Run the exact same CI suite that runs in GitHub Actions
just ci

# This executes:
# 1. just ci-lint - All lint checks (copyright, cpplint, uncrustify, etc.)
# 2. just ci-test - All tests with AddressSanitizer and UBSan
# Both steps run even if one fails, matching GitHub Actions behavior

# Expected results (as of 2026-01-05):
# - 141 tests total (32 lint + 109 functional/performance)
# - 29 tests skipped
# - 0 functional failures ✅
# - 4-5 performance test failures (need tuning for CI environment)
#
# Note: CI currently fails due to performance tests. These need threshold
# adjustments to pass in containerized CI environments.
```

**IMPORTANT:** Always run `just ci` before pushing to catch issues early!

### Code Quality

```bash
just lint         # Run linters
just fmt          # Format code
```

### Setup Verification

```bash
just check-setup  # Verify Docker, Cloudsmith, and build setup
```

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
├── Dockerfile               # Multi-stage build (base → dev/test)
├── docker-compose.yml       # Service orchestration
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
just dev

# Inside container: make changes, rebuild
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Debug

# Run tests
colcon test --event-handlers console_direct+

# Or from outside container
just compile  # Build
just test     # Test with sanitizers
```

### Adding New Tests

1. Create test file in `test/`
2. Add to `CMakeLists.txt` using `ament_add_gtest()`
3. Run: `just test`

## Troubleshooting

### Quick Diagnosis

```bash
just check-setup  # Checks Docker, API key, and build
```

### Cloudsmith Authentication Failed

```bash
# Verify your credentials in .env.local
cat .env.local

# Should contain:
# CLOUDSMITH_USERNAME=your-username
# CLOUDSMITH_API_KEY=your-api-key

# Rebuild without cache
just rebuild
```

### Build Fails with Missing robotops_msgs

The package depends on `robotops_msgs` from Cloudsmith. Run `just check-setup` to diagnose:
1. API key configured correctly
2. Docker buildx available
3. Build succeeds

### Tests Fail with AddressSanitizer

This is expected during development. Safety tests with ASan are designed to catch memory issues early.

```bash
just test  # Runs with detailed sanitizer output
```

## Related Issues

- **ROB-55**: This implementation (rmw_robotops)
- **ROB-54**: robotops_msgs package (TraceEvent.msg)
- **ROB-56**: Robot Agent span buffer and export
- **ROB-33**: Distributed Tracing epic (parent)
- **ROB-105**: Multi-DDS testing (CycloneDDS, etc.)

## License

Apache-2.0