# robotops-demo-agent

Lightweight standalone demo tool for evaluating `rmw_robotops` observability locally.

Subscribes to `/robotops/trace_events`, `/robotops/trace_context`, and `/rosout`, reconstructs distributed trace spans, and writes OTel-compatible Parquet files you can query with DuckDB or ROSQL.

**This is a demo/evaluation tool — not for production.** For production deployments with system metrics, TF monitoring, MCAP recording, offline buffering, and fleet management, see [robotops.com](https://robotops.com).

---

## Prerequisites

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

## 5. Query with DuckDB or ROSQL

```bash
# Install DuckDB CLI
curl -fsSL https://install.duckdb.org | sh

# Count spans collected
duckdb -c "SELECT count(*) FROM read_parquet('./telemetry/robotops_demo_agent/*/traces/*.parquet')"

# Recent errors
duckdb -c "
SELECT trace_id, span_name, status_code, start_time_ns
FROM read_parquet('./telemetry/robotops_demo_agent/*/traces/*.parquet')
WHERE status_code = 'ERROR'
ORDER BY start_time_ns DESC
LIMIT 20"

# Log messages with trace correlation
duckdb -c "
SELECT trace_id, span_id, severity_text, body
FROM read_parquet('./telemetry/robotops_demo_agent/*/logs/*.parquet')
ORDER BY observed_time_ns DESC
LIMIT 20"

# Join traces and logs for a specific trace
duckdb -c "
SELECT t.span_name, l.body, l.severity_text
FROM read_parquet('./telemetry/robotops_demo_agent/*/traces/*.parquet') t
JOIN read_parquet('./telemetry/robotops_demo_agent/*/logs/*.parquet') l
  ON t.trace_id = l.trace_id
WHERE t.trace_id = 'your-trace-id-here'"
```

With ROSQL:

```bash
rosql query "FROM traces WHERE status = 'ERROR' SINCE 5 min ago" \
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

### Traces schema (24 columns, OTel-compatible)

| Column | Type | Description |
|--------|------|-------------|
| `trace_id` | String | 16-byte hex trace ID |
| `span_id` | String | 8-byte hex span ID |
| `parent_span_id` | String (nullable) | Parent span ID |
| `span_name` | String | Operation name (topic or service) |
| `span_kind` | String | `PRODUCER`, `CONSUMER`, `CLIENT`, `SERVER`, `INTERNAL` |
| `start_time_ns` | Int64 | Span start (Unix nanoseconds) |
| `end_time_ns` | Int64 | Span end (Unix nanoseconds) |
| `duration_ns` | Int64 | `end_time_ns - start_time_ns` |
| `status_code` | String | `OK`, `ERROR`, `UNSET` |
| `status_message` | String (nullable) | Error message |
| `node_name` | String | ROS2 node name |
| `node_namespace` | String | ROS2 node namespace |
| `topic_name` | String | Topic or service name |
| `publisher_gid` | String (nullable) | Publisher GID (hex) |
| `content_hash` | UInt64 (nullable) | FNV-1a content hash for correlation |
| `rmw_implementation` | String | Underlying RMW (e.g. `rmw_fastrtps_cpp`) |
| `host_name` | String (nullable) | Hostname |
| `process_id` | UInt32 (nullable) | PID |
| `span_links` | String (nullable) | JSON array of linked span IDs |
| `service_name` | String | OTel service name |
| `resource_attributes` | String (nullable) | JSON resource attributes |
| `span_attributes` | String (nullable) | JSON span attributes |
| `events` | String (nullable) | JSON span events |
| `observed_time_ns` | Int64 | When the agent recorded this span |

### Logs schema (11 columns, OTel-compatible)

| Column | Type | Description |
|--------|------|-------------|
| `trace_id` | String (nullable) | Correlated trace ID |
| `span_id` | String (nullable) | Correlated span ID |
| `severity_number` | UInt8 | OTel severity number (1–24) |
| `severity_text` | String | `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL` |
| `body` | String | Log message text |
| `logger_name` | String | ROS2 logger name |
| `node_name` | String | ROS2 node name |
| `code_function` | String (nullable) | Source function name |
| `code_lineno` | Int32 (nullable) | Source line number |
| `service_name` | String | OTel service name |
| `observed_time_ns` | Int64 | When the agent recorded this log |

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

## Limitations

- **Not for production.** No system metrics, TF monitoring, MCAP recording, offline buffering, or fleet management. For production use, see [robotops.com](https://robotops.com).
- S3 writes are synchronous (blocking) — not suitable for high-throughput environments.
- Span reconstruction requires matched START/END events. Spans in-flight at shutdown will be incomplete.
- The correlation window (`--correlation-window-secs`) limits how far back publish events are retained for cross-process matching.
