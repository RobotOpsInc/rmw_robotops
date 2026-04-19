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

### Architecture

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

For a deep dive into component responsibilities, data flows, safety guarantees, trace context propagation, thread safety, and edge cases, see **[ARCHITECTURE.md](ARCHITECTURE.md)**.

---

## Installation

Building from source ensures ABI compatibility with your exact environment — compiler version, glibc, FastDDS version, etc. Dependencies are fetched from the public RobotOps APT repository (no credentials required).

**1. Add the RobotOps APT repository**

```bash
curl -fsSL https://apt.robotops.com/robotops-public-key.asc \
  | sudo gpg --dearmor -o /usr/share/keyrings/robotops-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/robotops-archive-keyring.gpg] https://apt.robotops.com noble main" \
  | sudo tee /etc/apt/sources.list.d/robotops.list
sudo apt update
```

**2. Configure rosdep to resolve RobotOps packages**

```bash
sudo mkdir -p /etc/ros/rosdep/sources.list.d
sudo tee /etc/ros/rosdep/sources.list.d/robotops.yaml > /dev/null <<EOF
robotops-config:
  ubuntu:
    - ros-jazzy-robotops-config

robotops_msgs:
  ubuntu:
    - ros-jazzy-robotops-msgs
EOF

rosdep update
```

**3. Declare the dependency in your ROS2 package's `package.xml`**

```xml
<depend>rmw_robotops</depend>
```

**4. Install dependencies and build**

```bash
cd ~/your_ros2_workspace
rosdep install --from-paths src --ignore-src -y
colcon build
```

### Try it out with the demo agent

`robotops-demo-agent` is a lightweight local tool that subscribes to trace events, reconstructs spans, and writes Parquet files you can query with ROSQL — a quick way to see rmw_robotops in action end-to-end.

```bash
# Install ROSQL (more options at https://rosql.org)
curl -fsSL https://rosql.org/install.sh | sh

# Build the demo agent
source /opt/ros/jazzy/setup.bash && cd demo-agent && cargo build --release

# Run (the session path is printed on startup)
./target/release/robotops-demo-agent

# Query with ROSQL — replace 20260403-141530 with your session path
rosql query "FROM traces SINCE 1h" \
  --backend parquet \
  --url ./telemetry/robotops_demo_agent/20260403-141530/
```

For full setup instructions, CLI reference, S3 configuration, and schema documentation see **[demo-agent/README.md](demo-agent/README.md)**.

> `rmw_robotops` is production-ready middleware. `robotops-demo-agent` is a lightweight local evaluation tool — for production-grade telemetry with fleet management, MCAP recording, and offline buffering, see [robotops.com](https://robotops.com).

---

## FAQ

**Is rmw_robotops ready for production use?**
`rmw_robotops` is in **production beta**. The safety architecture is fully implemented and validated — the 8 safety guarantees are enforced by design. Performance benchmarking is underway (see [#41](https://github.com/RobotOpsInc/rmw_robotops/issues/41)). Early adopters are running it in production today; we recommend monitoring the benchmark results before deploying on latency-sensitive workloads.

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
