#!/usr/bin/env bash
# Publisher container entrypoint.
# Sources the rmw_robotops colcon overlay so RMW_IMPLEMENTATION=rmw_robotops resolves,
# then runs turtlesim_node (headless via Xvfb) + the scripted drive scenario.
set -euo pipefail

source /opt/ros/jazzy/setup.bash
source /workspace/install/setup.bash

echo "[publisher] RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}"
echo "[publisher] ROBOTOPS_UNDERLYING_RMW=${ROBOTOPS_UNDERLYING_RMW}"
echo "[publisher] ROS_DOMAIN_ID=${ROS_DOMAIN_ID}"
[ -n "${ROBOTOPS_TRACE_TOPIC_FILTER:-}" ] && \
    echo "[publisher] ROBOTOPS_TRACE_TOPIC_FILTER=${ROBOTOPS_TRACE_TOPIC_FILTER}"

# Start a virtual framebuffer so turtlesim_node can create its window
Xvfb :99 -screen 0 800x600x24 &
XVFB_PID=$!
export DISPLAY=:99

# Give Xvfb a moment to initialise
sleep 1

# Start turtlesim in the background (both this process and turtlesim_node inherit
# RMW_IMPLEMENTATION so rmw_robotops traces both sides of the pub/sub)
ros2 run turtlesim turtlesim_node &
TURTLESIM_PID=$!

# Wait for turtlesim to advertise its services before driving
sleep 3

echo "[publisher] Running drive_turtle.py ..."
python3 /tests/scripts/drive_turtle.py

echo "[publisher] Scenario finished — shutting down"
kill "$TURTLESIM_PID" 2>/dev/null || true
wait "$TURTLESIM_PID" 2>/dev/null || true
kill "$XVFB_PID" 2>/dev/null || true
