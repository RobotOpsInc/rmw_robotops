#!/usr/bin/env bash
# Combined container entrypoint: runs turtlesim + drive_turtle.py (under
# RMW_IMPLEMENTATION=rmw_robotops) and robotops-demo-agent (stock FastDDS)
# in the same container so they communicate via loopback DDS — no multicast
# discovery needed, works reliably on Docker Desktop for macOS.
set -eo pipefail

# ROS2 setup.bash references unset variables; incompatible with set -u
source /opt/ros/jazzy/setup.bash
# e2e_install is the dedicated RelWithDebInfo overlay, isolated from the
# ASan-compiled dev/test build that lives in install/.
source /workspace/src/rmw_robotops/e2e_install/local_setup.bash

SETTLE_SECS="${E2E_SETTLE_SECS:-8}"

echo "[combined] RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}"
echo "[combined] ROBOTOPS_UNDERLYING_RMW=${ROBOTOPS_UNDERLYING_RMW}"
echo "[combined] ROS_DOMAIN_ID=${ROS_DOMAIN_ID}"
[ -n "${ROBOTOPS_TRACE_TOPIC_FILTER:-}" ] && \
    echo "[combined] ROBOTOPS_TRACE_TOPIC_FILTER=${ROBOTOPS_TRACE_TOPIC_FILTER}"

# ── Xvfb ─────────────────────────────────────────────────────────────────────
Xvfb :99 -screen 0 800x600x24 &
XVFB_PID=$!
export DISPLAY=:99
sleep 1

# ── turtlesim (under rmw_robotops so its pub/sub traffic is traced) ───────────
ros2 run turtlesim turtlesim_node &
TURTLESIM_PID=$!
sleep 3

# ── demo-agent (stock FastDDS — unset rmw_robotops vars to avoid trace loop) ──
env -u RMW_IMPLEMENTATION \
    -u ROBOTOPS_UNDERLYING_RMW \
    -u ROBOTOPS_TRACING_ENABLED \
    robotops-demo-agent \
        --output /data \
        --batch-size 50 \
        --flush-interval-ms 1000 \
        --correlation-window-secs 5 \
        --robot-id "e2e-test-robot" \
        --organization-id "e2e-test-org" &
AGENT_PID=$!

# Give the agent a moment to subscribe before the scenario starts publishing
sleep 2

# ── drive the turtlesim scenario ──────────────────────────────────────────────
echo "[combined] Running drive_turtle.py ..."
python3 /tests/scripts/drive_turtle.py

echo "[combined] Scenario finished — settling ${SETTLE_SECS}s for final events..."
sleep "$SETTLE_SECS"

# ── flush agent ───────────────────────────────────────────────────────────────
echo "[combined] Sending SIGINT to agent (final Parquet flush)..."
kill -INT "$AGENT_PID" 2>/dev/null || true
wait "$AGENT_PID" 2>/dev/null || true

echo "[combined] Agent flushed. Done."

# ── cleanup ───────────────────────────────────────────────────────────────────
kill "$TURTLESIM_PID" 2>/dev/null || true
wait "$TURTLESIM_PID" 2>/dev/null || true
kill "$XVFB_PID" 2>/dev/null || true
