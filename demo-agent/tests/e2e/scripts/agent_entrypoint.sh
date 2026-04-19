#!/usr/bin/env bash
# Agent container entrypoint.
# Runs robotops-demo-agent with settings tuned for the e2e test:
#   - small batch + short flush interval so data is on disk promptly
#   - short correlation window so the test doesn't need to wait 30 s
#   - writes to /data (bind-mounted shared volume)
set -euo pipefail

source /opt/ros/jazzy/setup.bash

echo "[agent] ROS_DOMAIN_ID=${ROS_DOMAIN_ID}"
echo "[agent] Writing Parquet to /data ..."

exec robotops-demo-agent \
    --output /data \
    --batch-size 50 \
    --flush-interval-ms 1000 \
    --correlation-window-secs 5 \
    --robot-id "e2e-test-robot" \
    --organization-id "e2e-test-org"
