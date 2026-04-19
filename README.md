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

## Getting started: end-to-end evaluation

The quickest way to see rmw_robotops + ROSQL in action is to run the `robotops-demo-agent` locally.

```bash
# Build
source /opt/ros/jazzy/setup.bash && cd demo-agent && cargo build --release

# Run (writes Parquet to ./telemetry/ — note the session path printed on startup)
./target/release/robotops-demo-agent

# Query with ROSQL (replace <session> with the timestamp directory printed on startup)
rosql query "FROM traces SINCE 1h" \
  --backend parquet \
  --url ./telemetry/robotops_demo_agent/<session>/
```

For full setup instructions, CLI reference, S3 configuration, and schema documentation see **[demo-agent/README.md](demo-agent/README.md)**.

> `robotops-demo-agent` is a demo/evaluation tool. For production deployments with system metrics, TF monitoring, MCAP recording, offline buffering, and fleet management, see [robotops.com](https://robotops.com).

---

## FAQ

**Is this ready for production use?**
Yes. rmw_robotops is designed as a safety-critical middleware layer with 8 safety guarantees. It is used in production robotics deployments.

**Does rmw_robotops add latency to my messages?**
Tracing adds minimal overhead (target: <1µs median per message). Real message delivery always completes first — tracing is secondary and cannot block or delay it. The queue push is lock-free and non-blocking. See [Benchmarks](#benchmarks) for measured results as they become available.

**Can tracing crash my robot?**
No. All tracing code is wrapped in `catch(...)` blocks. Exceptions are swallowed and logged. If consecutive tracing failures exceed the configured threshold, tracing auto-disables, leaving the underlying RMW running cleanly.

**Does it modify my DDS messages?**
No. rmw_robotops is purely passive and observational. It does not modify message payloads, QoS settings, or the DDS wire format in any way.

**Which DDS implementations are supported?**
All of them — FastDDS, CycloneDDS, Connext DDS, and any future RMW implementation. rmw_robotops is fully DDS-agnostic and delegates to whatever you specify via `ROBOTOPS_UNDERLYING_RMW`.

**Can I disable tracing at runtime?**
Yes. Set `ROBOTOPS_TRACING_ENABLED=false` to reduce overhead to a single boolean check per operation — no queue, no background thread, pure passthrough.

---

## Benchmarks

> **Coming soon** — see [#41](https://github.com/RobotOpsInc/rmw_robotops/issues/41)

The benchmark implementation is in progress. Performance targets:

| Metric | Target |
|--------|--------|
| Added latency (median) | < 1µs per message |
| CPU overhead | < 5% vs underlying RMW |
| Hot-path allocations | Zero |

---

## Installation

### Option 1: Binary Package

```bash
# Add RobotOps APT repository (one-time setup)
curl -fsSL https://apt.robotops.com/robotops-public-key.asc \
  | sudo gpg --dearmor -o /usr/share/keyrings/robotops-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/robotops-archive-keyring.gpg] https://apt.robotops.com noble main" \
  | sudo tee /etc/apt/sources.list.d/robotops.list

# Install pre-built binary
sudo apt update
sudo apt install ros-jazzy-rmw-robotops
```

### Option 2: Build from Source (Recommended for Maximum Compatibility)

Building from source ensures maximum ABI compatibility with your exact environment (compiler version, dependency versions, glibc, etc.). All dependencies (`robotops_msgs`, `robotops-config`) compile together in your environment for perfect ABI alignment.

**Setup:**

```bash
# Add RobotOps APT repository (one-time setup)
curl -fsSL https://apt.robotops.com/robotops-public-key.asc \
  | sudo gpg --dearmor -o /usr/share/keyrings/robotops-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/robotops-archive-keyring.gpg] https://apt.robotops.com noble main" \
  | sudo tee /etc/apt/sources.list.d/robotops.list

# Configure rosdep to find RobotOps packages
mkdir -p /etc/ros/rosdep/sources.list.d
sudo tee /etc/ros/rosdep/sources.list.d/robotops.yaml > /dev/null <<EOF
robotops-config:
  ubuntu:
    - ros-jazzy-robotops-config

robotops_msgs:
  ubuntu:
    - ros-jazzy-robotops-msgs

rmw_robotops:
  ubuntu:
    - ros-jazzy-rmw-robotops
EOF

rosdep update
```

**In your ROS2 package's `package.xml`:**

```xml
<depend>rmw_robotops</depend>
```

**Build:**

```bash
cd ~/your_ros2_workspace
rosdep install --from-paths src --ignore-src -y
colcon build
```

`rosdep` automatically fetches the source packages from `apt.robotops.com`, and `colcon` builds them all together in your environment.

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

## Configuration

`rmw_robotops` uses a layered configuration system with the following precedence (highest to lowest):

### Configuration Precedence

1. **Environment Variables** (highest priority)
2. **Custom YAML Config** via `ROBOTOPS_CONFIG_PATH`
3. **System YAML Config** at `/etc/robotops/config.yaml`
4. **Package Defaults** from `robotops-config`

Each layer overrides settings from lower layers. This allows:
- System-wide defaults in `/etc/robotops/config.yaml` for production deployments
- Per-robot customization via `ROBOTOPS_CONFIG_PATH`
- Quick overrides via environment variables for testing

### Environment Variables

```bash
# Tracing control
export ROBOTOPS_TRACING_ENABLED=true          # Enable/disable tracing (default: from config)
export ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp  # Underlying RMW implementation (required)
export ROBOTOPS_FAILURE_THRESHOLD=100         # Auto-disable after N failures (default: 100)

# Custom config path
export ROBOTOPS_CONFIG_PATH=/path/to/custom/config.yaml  # Override default config location
```

### YAML Configuration Format

Create a YAML configuration file at `/etc/robotops/config.yaml` (system-wide) or specify a custom path:

```yaml
schema_version: "1.0.0"
tracing:
  enabled: true
  underlying_rmw: "rmw_fastrtps_cpp"
  performance:
    failure_threshold: 100
```

**Field descriptions:**
- `schema_version`: Configuration schema version (currently "1.0.0")
- `tracing.enabled`: Enable distributed tracing (bool)
- `tracing.underlying_rmw`: RMW implementation to delegate to (string)
- `tracing.performance.failure_threshold`: Auto-disable threshold for consecutive failures (uint32)

### Configuration Examples

**Example 1: Production deployment with system config**

```bash
# /etc/robotops/config.yaml (system-wide defaults)
schema_version: "1.0.0"
tracing:
  enabled: true
  underlying_rmw: "rmw_fastrtps_cpp"
  performance:
    failure_threshold: 100
```

```bash
# Launch node (uses system config)
export RMW_IMPLEMENTATION=rmw_robotops
ros2 run my_package my_node
```

**Example 2: Custom robot configuration**

```bash
# /opt/robot/config/rmw.yaml (robot-specific config)
schema_version: "1.0.0"
tracing:
  enabled: true
  underlying_rmw: "rmw_cyclonedds_cpp"
  performance:
    failure_threshold: 50
```

```bash
# Launch node with custom config
export RMW_IMPLEMENTATION=rmw_robotops
export ROBOTOPS_CONFIG_PATH=/opt/robot/config/rmw.yaml
ros2 run my_package my_node
```

**Example 3: Temporary testing override**

```bash
# Quick disable for testing (env var overrides all config files)
export RMW_IMPLEMENTATION=rmw_robotops
export ROBOTOPS_TRACING_ENABLED=false
ros2 run my_package my_node
```

### Configuration Logging

On startup, `rmw_robotops` logs the final configuration:

```
[INFO] [rmw_robotops]: No system config found at /etc/robotops/config.yaml (using defaults)
[INFO] [rmw_robotops]: Configuration loaded: schema_version=1.0.0, tracing=enabled, underlying_rmw=rmw_fastrtps_cpp, failure_threshold=100
```

If a custom config path is specified but invalid:

```
[WARN] [rmw_robotops]: ROBOTOPS_CONFIG_PATH=/invalid/path.yaml specified but file not found or invalid
```

## Versioning

This repository uses **semantic versioning** expressed in `package.xml` and enforced via git tags (e.g., `v0.1.4`, `v0.2.0`).

### Lockstep Major Versions

**Major versions** move in lockstep across the entire RobotOps ecosystem:

- `robotops_config`
- `robotops_msgs`
- `rmw_robotops` (this repository)
- `robot_agent`

When any component introduces a breaking change, all components bump to the next major version together (e.g., all move from `v1.x.x` to `v2.0.0`).

### Independent Minor and Patch Versions

Between major version boundaries, each component evolves independently:

- **Minor versions** (e.g., `v0.1.0` → `v0.2.0`) add backward-compatible features
- **Patch versions** (e.g., `v0.1.4` → `v0.1.5`) fix bugs without breaking compatibility

**Backward compatibility is maintained by design** for all minor and patch releases within the same major version.

### Examples

```
robotops_config v0.4.14   ← May be ahead (added new config section)
robotops_msgs v0.3.2      ← May be behind (stable, no changes needed)
rmw_robotops v0.1.5       ← Independent evolution

rmw_robotops v1.0.0       ← Breaking change: all components bump major version
robotops_config v1.0.0
robotops_msgs v1.0.0
```

**Recommendation:** Always reference a specific version tag in production deployments for stability.

---

## Architecture

**For detailed architecture documentation, see [ARCHITECTURE.md](ARCHITECTURE.md).**

This document covers:
- Component architecture and responsibilities
- Detailed data flows (publish, subscribe, services)
- Safety guarantees and implementation
- Trace context propagation (intra-process and cross-process)
- Thread safety model
- Edge cases and performance characteristics

**High-level overview:**

```
┌─────────────────────────────────────────────────────────────┐
│                    Robot Node Process                       │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐   │
│   │              rmw_robotops                           │   │
│   │                                                     │   │
│   │   rmw_publish() → [Real Message First]              │   │
│   │                 → [Best-Effort Trace Event]         │   │
│   │                                                     │   │
│   │   rmw_take()    → [Extract Context from DDS]        │   │
│   │                 → [Set Thread-Local Context]        │   │
│   │                                                     │   │
│   │   Background Thread → [/robotops/trace_events]      │   │
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

## Development

For development setup, workflow, testing, and contribution guidelines, see **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## License

Apache-2.0
