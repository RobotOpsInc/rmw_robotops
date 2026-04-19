# robotops-demo-agent

Lightweight local evaluation companion for `rmw_robotops`.

Subscribes to `/robotops/trace_events`, `/robotops/trace_context`, and `/rosout`, reconstructs distributed trace spans, and writes OTel-compatible Parquet files you can query with ROSQL.

`rmw_robotops` is production-ready middleware. This demo agent is a local evaluation tool — for production-grade telemetry with fleet management, MCAP recording, and offline buffering, see [robotops.com](https://robotops.com).

---

## Pre-compiled binaries

Pre-compiled Linux binaries (amd64 and arm64) are attached to each
[GitHub Release](https://github.com/RobotOpsInc/rmw_robotops/releases):

```bash
# amd64
curl -fsSL -o robotops-demo-agent \
  https://github.com/RobotOpsInc/rmw_robotops/releases/latest/download/robotops-demo-agent-linux-amd64
chmod +x robotops-demo-agent
./robotops-demo-agent --help
```

```bash
# arm64 (e.g. Raspberry Pi 5, Jetson running Ubuntu 24.04)
curl -fsSL -o robotops-demo-agent \
  https://github.com/RobotOpsInc/rmw_robotops/releases/latest/download/robotops-demo-agent-linux-arm64
chmod +x robotops-demo-agent
./robotops-demo-agent --help
```

> **Note:** The binary requires ROS2 Jazzy runtime libraries and `ros-jazzy-robotops-msgs`
> to be installed. Source `/opt/ros/jazzy/setup.bash` before running.

---

## Prerequisites (build from source)

- **ROS2 Jazzy** installed (see [ROS2 installation](https://docs.ros.org/en/jazzy/Installation.html))
- **Rust stable** toolchain (`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`)
- **libclang** for r2r's bindgen step: `sudo apt install libclang-dev clang`

---

## 1. Install rmw_robotops

```bash
# Add RobotOps APT repository (one-time setup)
curl -fsSL https://apt.robotops.com/robotops-public-key.asc \
  | sudo gpg --dearmor -o /usr/share/keyrings/robotops-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/robotops-archive-keyring.gpg] https://apt.robotops.com noble main" \
  | sudo tee /etc/apt/sources.list.d/robotops.list

sudo apt update
sudo apt install ros-jazzy-rmw-robotops ros-jazzy-robotops-msgs
```

---

## 2. Build the demo agent

```bash
source /opt/ros/jazzy/setup.bash
cd demo-agent
cargo build --release
```

The first build downloads and compiles dependencies (pure Rust — no C++ compilation). Subsequent builds are incremental.

The binary is at `target/release/robotops-demo-agent`.

---

## 3. Launch your ROS2 system with tracing

```bash
export RMW_IMPLEMENTATION=rmw_robotops
export ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp

ros2 launch my_robot my_launch.py
# or: ros2 run my_package my_node
```

---

## 4. Run the demo agent

```bash
# Local output (default)
./target/release/robotops-demo-agent

# Custom output directory
./target/release/robotops-demo-agent -o /data/robot-traces

# S3 output
./target/release/robotops-demo-agent -o s3://my-bucket/robot-01

# S3-compatible (MinIO, Ceph, etc.)
AWS_ENDPOINT_URL=http://minio:9000 \
  ./target/release/robotops-demo-agent -o s3://traces/robot-01
```

On startup you will see:

```
robotops-demo-agent v0.9.0
Writing to:    ./telemetry/robotops_demo_agent/20260403-141530/
Storage limit: 200 MB

Subscribing to /robotops/trace_events, /robotops/trace_context, /rosout...
Press Ctrl-C to stop.

For production-grade telemetry with fleet management, MCAP recording,
and offline buffering, visit https://robotops.com
Questions? hello@robotops.com
```

Press **Ctrl-C** to flush and exit cleanly.

---

## 5. Install ROSQL

```bash
# More installation options at https://rosql.org
curl -fsSL https://rosql.org/install.sh | sh
```

---

## 6. Query with ROSQL

The session path is printed on startup (e.g. `./telemetry/robotops_demo_agent/20260403-141530/`) — substitute your own when running these queries.

```bash
# Replace 20260403-141530 with the session path printed on startup

# All spans from the last hour
rosql query "FROM traces SINCE 1h" \
  --backend parquet \
  --url ./telemetry/robotops_demo_agent/20260403-141530/

# Publish spans only
rosql query "FROM traces WHERE span_kind = 'PRODUCER' SINCE 1h" \
  --backend parquet \
  --url ./telemetry/robotops_demo_agent/20260403-141530/

# Errors
rosql query "FROM traces WHERE status_code = 'ERROR' SINCE 1h" \
  --backend parquet \
  --url ./telemetry/robotops_demo_agent/20260403-141530/

# Log messages
rosql query "FROM logs SINCE 1h" \
  --backend parquet \
  --url ./telemetry/robotops_demo_agent/20260403-141530/

# Logs correlated to a trace
rosql query "FROM logs WHERE trace_id != '' SINCE 1h" \
  --backend parquet \
  --url ./telemetry/robotops_demo_agent/20260403-141530/
```

---

## CLI reference

| Flag | Default | Env var | Description |
|------|---------|---------|-------------|
| `-o`, `--output` | `./telemetry` | `ROBOTOPS_DEMO_OUTPUT` | Output path: local dir or `s3://bucket/prefix` |
| `--limit-mb` | `200` | `ROBOTOPS_DEMO_LIMIT_MB` | Storage limit in MB; agent exits gracefully when reached |
| `--batch-size` | `1000` | `ROBOTOPS_DEMO_BATCH_SIZE` | Records buffered before writing a Parquet file |
| `--flush-interval-ms` | `5000` | `ROBOTOPS_DEMO_FLUSH_INTERVAL_MS` | How often to flush pending records (ms) |
| `--correlation-window-secs` | `30` | `ROBOTOPS_DEMO_CORRELATION_WINDOW_SECS` | How long to keep unmatched publish events |
| `--correlation-tolerance-ns` | `10000000` | `ROBOTOPS_DEMO_CORRELATION_TOLERANCE_NS` | Timestamp tolerance for cross-process correlation (ns) |
| `--domain-id` | `0` | `ROS_DOMAIN_ID` | ROS2 domain ID |
| `--robot-id` | _(empty)_ | `ROBOTOPS_ROBOT_ID` | Robot identifier written to `resource_attributes["robot.id"]` (used by ROSQL `WHERE robot_id = '...'`) |
| `--organization-id` | _(empty)_ | `ROBOTOPS_ORG_ID` | Organization identifier written to `resource_attributes["organization.id"]` |

### S3 environment variables

| Variable | Description |
|----------|-------------|
| `AWS_PROFILE` | AWS credentials profile |
| `AWS_REGION` | AWS region (e.g. `us-east-1`) |
| `AWS_ACCESS_KEY_ID` | Static credentials |
| `AWS_SECRET_ACCESS_KEY` | Static credentials |
| `AWS_ENDPOINT_URL` | Override endpoint for S3-compatible storage (MinIO, Ceph, etc.) |

---

## Output format

Files are written under:

```
<output>/robotops_demo_agent/<yyyymmdd-hhmmss>/
  traces/
    part-0001.parquet
    part-0002.parquet
    ...
  logs/
    part-0001.parquet
    ...
```

Each file is write-once. New files are created on each flush when the buffer is non-empty. Use glob patterns to query across all files in a session or across sessions.

### Traces schema — ROSQL OtelPostgres-compatible

The schema is a **superset** of the [ROSQL OtelPostgres profile](https://rosql.org). The core columns come first (used by ROSQL queries), followed by ROS2-specific extras that ROSQL ignores.

**Core columns (ROSQL OtelPostgres profile)**

| Column | Arrow type | Description |
|--------|-----------|-------------|
| `timestamp` | `Timestamp(Microsecond, UTC)` | Span start time — used by ROSQL `SINCE`/`UNTIL` |
| `trace_id` | String | Hex trace ID |
| `span_id` | String | Hex span ID |
| `parent_span_id` | String | Parent span ID (`""` when none; subscribe spans get the cross-process publish span as parent) |
| `span_name` | String | e.g. `publish /chatter` |
| `span_kind` | String | `PRODUCER`, `CONSUMER`, `SERVER`, `INTERNAL` |
| `service_name` | String | Derived from ROS2 node: `/{namespace}/{node_name}` |
| `duration` | Int64 | Span duration in nanoseconds |
| `status_code` | String | `OK`, `ERROR`, `UNSET` |
| `span_attributes` | String (JSON) | ROS2 attributes: `ros.node`, `ros.topic`, `ros.message_type`, `ros.publisher_gid`, `ros.content_hash`, `ros.sequence_number`, `ros.source_timestamp_ns`, `ros.message_size_bytes`, `ros.dds.domain_id`, `ros.correlation_method`, `ros.correlation.publish_span_id` |
| `resource_attributes` | String (JSON) | `robot.id`, `organization.id` (set via `--robot-id` / `--organization-id`) |

**Extra columns (ROS2-specific, not used by ROSQL)**

`operation`, `start_time_ns`, `end_time_ns`, `ros_topic`, `ros_node_name`, `ros_node_namespace`, `ros_message_type`, `ros_publisher_gid`, `ros_content_hash`, `ros_sequence_number`, `ros_source_timestamp_ns`, `ros_message_size_bytes`, `ros_dds_domain_id`, `ros_correlation_method`, `correlated_publish_span_id`, `span_links`, `written_at_ns`.

**Example ROSQL queries:**

```bash
# Replace 20260403-141530 with the session path printed on startup

# Filter by topic
rosql query "FROM traces WHERE span_name LIKE 'publish /chatter%' SINCE 5m" \
  --backend parquet --url ./telemetry/robotops_demo_agent/20260403-141530/

# Filter by robot
rosql query "FROM traces WHERE robot_id = 'my-robot-01' SINCE 1h" \
  --backend parquet --url ./telemetry/robotops_demo_agent/20260403-141530/
```

### Logs schema — ROSQL OtelPostgres-compatible

| Column | Arrow type | Description |
|--------|-----------|-------------|
| `timestamp` | `Timestamp(Microsecond, UTC)` | Log emit time |
| `trace_id` | String | Correlated trace ID (`""` when absent) |
| `span_id` | String | Correlated span ID (`""` when absent) |
| `severity_text` | String | `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL` |
| `severity_number` | Int32 | OTel severity number (1–24) |
| `service_name` | String | ROS2 logger name (proxy for service) |
| `body` | String | Log message text |
| `resource_attributes` | String (JSON) | Same as traces (`robot.id`, `organization.id`) |
| `log_attributes` | String (JSON) | `logger.name`, `code.filepath`, `code.function`, `code.lineno` |
| `written_at_ns` | Int64 | When the agent wrote this record (debug) |

---

## Docker (optional)

A `Dockerfile` is provided for building in an isolated environment:

```bash
# Build the image (includes ROS2 Jazzy + Rust + robotops-msgs)
docker build -t robotops-demo-agent-dev .

# Build the binary inside the container
docker run --rm \
  -v "$(pwd):/workspace/demo-agent" \
  -w /workspace/demo-agent \
  robotops-demo-agent-dev \
  bash -c "source /opt/ros/jazzy/setup.bash && cargo build --release"
```

---

## E2E test rig

An end-to-end Docker-based test verifies that `demo-agent` Parquet output is
queryable by `rosql` against a live ROS2 turtlesim scenario. It runs manually
(not part of regular PR CI):

```bash
just e2e
```

See [`tests/e2e/README.md`](tests/e2e/README.md) for full details, options, and
troubleshooting.

---

## Limitations

- **Demo agent only.** `rmw_robotops` is production-ready, but this agent is a local evaluation tool — no system metrics, TF monitoring, MCAP recording, offline buffering, or fleet management. For production use, see [robotops.com](https://robotops.com).
- S3 writes are synchronous (blocking) — not suitable for high-throughput environments.
- Span reconstruction requires matched START/END events. Spans in-flight at shutdown will be incomplete.
- The correlation window (`--correlation-window-secs`) limits how far back publish events are retained for cross-process matching.
