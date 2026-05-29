# demo-agent E2E test rig

Verifies that a real ROS2 Jazzy robot scene traced through `rmw_robotops`
produces Parquet output that `rosql` can actually query.

## What it does

1. Builds `rmw_robotops` from source into a Docker colcon overlay.
2. Runs a **turtlesim scenario** (two processes under `RMW_IMPLEMENTATION=rmw_robotops`):
   - `turtlesim_node` — publishes `/turtle1/pose`, handles services and actions.
   - `drive_turtle.py` — calls `/spawn`, `/kill`, `/turtle1/set_pen` (service spans);
     sends `/turtle1/rotate_absolute` goal (action traffic); publishes
     `/turtle1/cmd_vel` and `/turtle2/cmd_vel` (producer spans);
     subscribes to `/turtle1/pose` (consumer spans + log correlation).
3. Captures the session with `robotops-demo-agent` into Parquet.
4. Runs a `rosql` assertion harness over the output.

## Running locally

All execution happens inside Docker — no ROS2 installation required on the host.

```bash
cd <repo-root>/demo-agent
just e2e
```

To keep the Parquet output for inspection after the run:

```bash
E2E_KEEP_OUTPUT=1 just e2e
```

To exercise the topic-filter variant (asserts `/turtle2` spans are absent):

```bash
E2E_FILTER_MODE=1 just e2e
```

## Options

| Env var | Default | Description |
|---|---|---|
| `E2E_SETTLE_SECS` | `8` | Seconds to wait after publisher exits before SIGINT |
| `E2E_ROS_DOMAIN_ID` | `42` | DDS domain ID (change if 42 conflicts on your machine) |
| `E2E_FILTER_MODE` | `0` | Set to `1` to run the topic-filter variant |
| `E2E_KEEP_OUTPUT` | `0` | Set to `1` to preserve the Parquet session dir |
| `E2E_OUTPUT_DIR` | auto temp | Override the Parquet output directory |
| `APT_REPO_URL` | `https://apt.robotops.com` | Override the TraceHouse APT repo |

## Query suite

See `queries.yml` for the full list. Short summary:

- **`>=1` (must return rows):** `traces_any`, `traces_producer`, `traces_consumer`,
  `traces_server`, `traces_cmd_vel`, `traces_pose_subscriber`,
  `traces_correlated_consumer`, `logs_any`, `logs_info_severity`,
  `logs_correlated_to_traces`, `traces_resource_attrs_present`
- **`>=0` (parse-only):** `traces_error_status`, `traces_action_related`,
  `logs_warn_severity`
- **`filter_mode_zero` (==0 when `E2E_FILTER_MODE=1`):** `traces_filtered_topic`

## Troubleshooting

**`No demo-agent session found`** — the agent container may have exited before
writing. Increase `E2E_SETTLE_SECS` or check agent logs with
`docker logs e2e_agent_run`.

**DDS discovery failures** — if publisher and agent can't see each other, try
changing `E2E_ROS_DOMAIN_ID` to avoid collision with other ROS2 nodes on the host.

**rosql not found** — the query image installs rosql via
`curl -fsSL https://rosql.org/install.sh | sh`. If the install fails (e.g., network
timeout in CI), the error appears during `docker build` for `Dockerfile.query`.
