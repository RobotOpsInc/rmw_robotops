#!/usr/bin/env bash
# E2E orchestrator for demo-agent + ROSQL validation.
#
# Workflow:
#   1. Build rmw_robotops dev image and compile a clean RelWithDebInfo overlay
#      into e2e_build/ / e2e_install/ inside the source tree (isolated from the
#      ASan-compiled dev/test build).
#   2. Build the demo-agent CI image (Rust toolchain base for cargo build).
#   3. Build the combined e2e image (turtlesim + drive_turtle + demo-agent) and
#      the query image.
#   4. Run the combined container to completion:
#        - turtlesim_node under RMW_IMPLEMENTATION=rmw_robotops
#        - robotops-demo-agent (stock FastDDS, no trace loop)
#        - drive_turtle.py executes the scripted scenario
#        - agent is SIGINT'd inside the container after a settle window
#      Both processes share loopback DDS — no cross-container multicast needed.
#   5. Run the ROSQL assertion harness against the Parquet output.
#
# Options (env vars):
#   E2E_SETTLE_SECS        seconds to wait after scenario ends before SIGINT (default: 8)
#   E2E_ROS_DOMAIN_ID      ROS domain ID to isolate from host traffic (default: 42)
#   E2E_FILTER_MODE        set to 1 to run the topic-filter variant (default: 0)
#   E2E_KEEP_OUTPUT        set to 1 to skip cleanup of the Parquet output dir (default: 0)
#   E2E_OUTPUT_DIR         override the output directory (default: auto temp dir)
#   APT_REPO_URL           override the TraceHouse APT repo (default: https://apt.robotops.com)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
E2E_DIR="$(dirname "$SCRIPT_DIR")"
DEMO_AGENT_DIR="$(dirname "$(dirname "$E2E_DIR")")"
REPO_ROOT="$(dirname "$DEMO_AGENT_DIR")"

SETTLE_SECS="${E2E_SETTLE_SECS:-8}"
ROS_DOMAIN_ID="${E2E_ROS_DOMAIN_ID:-42}"
FILTER_MODE="${E2E_FILTER_MODE:-0}"
KEEP_OUTPUT="${E2E_KEEP_OUTPUT:-0}"
APT_REPO_URL="${APT_REPO_URL:-https://apt.robotops.com}"

OUTPUT_DIR="${E2E_OUTPUT_DIR:-}"
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="$(mktemp -d /tmp/e2e-rosql-XXXXXX)"
fi

log() { echo "[e2e] $*"; }

cleanup() {
    if [ "$KEEP_OUTPUT" = "0" ] && [ -n "$OUTPUT_DIR" ]; then
        log "Removing output dir: $OUTPUT_DIR"
        rm -rf "$OUTPUT_DIR"
    else
        log "Parquet output preserved at: $OUTPUT_DIR"
    fi
}
trap cleanup EXIT

# ── Step 1: Build rmw_robotops dev image ─────────────────────────────────────
log "Step 1: Building rmw_robotops dev image..."
docker compose \
    -p rmw_robotops \
    -f "$REPO_ROOT/docker-compose.yml" \
    build dev \
    --build-arg "APT_REPO_URL=${APT_REPO_URL}"

# Build rmw_robotops into DEDICATED e2e build/install dirs (e2e_build/ and e2e_install/)
# inside the source tree. colcon's default ./build and ./install dirs are shared with
# the dev/test workflow and may contain ASan-compiled artifacts; using separate base
# dirs guarantees a clean RelWithDebInfo build used by the combined container.
log "Step 1b: Building colcon overlay into e2e_build/e2e_install (RelWithDebInfo)..."
docker run --rm \
    -v "${REPO_ROOT}:/workspace/src/rmw_robotops" \
    rmw_robotops:dev \
    bash -c "
        source /opt/ros/jazzy/setup.bash
        cd /workspace/src/rmw_robotops
        colcon build \
            --build-base e2e_build \
            --install-base e2e_install \
            --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            --event-handlers console_direct+
    "

# ── Step 2: Build demo-agent CI image ────────────────────────────────────────
log "Step 2: Building robotops-demo-agent:ci image..."
docker build \
    -t robotops-demo-agent:ci \
    --build-arg "APT_REPO_URL=${APT_REPO_URL}" \
    "$DEMO_AGENT_DIR"

# ── Step 3: Build e2e images ──────────────────────────────────────────────────
log "Step 3: Building e2e images..."

docker build \
    -t rmw_robotops-e2e-combined:latest \
    -f "$E2E_DIR/Dockerfile.combined" \
    "$REPO_ROOT" \
    --build-arg "APT_REPO_URL=${APT_REPO_URL}"

docker build \
    -t rmw_robotops-e2e-query:latest \
    -f "$E2E_DIR/Dockerfile.query" \
    "$E2E_DIR"

# ── Step 4: Run combined container to completion ──────────────────────────────
log "Step 4: Running combined container (turtlesim + demo-agent + scenario)..."

FILTER_ENV=()
if [ "$FILTER_MODE" = "1" ]; then
    log "  Filter mode: ROBOTOPS_TRACE_TOPIC_FILTER='^/turtle1/.*'"
    FILTER_ENV=(-e "ROBOTOPS_TRACE_TOPIC_FILTER=^/turtle1/.*")
fi

docker run \
    --rm \
    -e "ROS_DOMAIN_ID=${ROS_DOMAIN_ID}" \
    -e "RMW_IMPLEMENTATION=rmw_robotops" \
    -e "ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp" \
    -e "ROBOTOPS_TRACING_ENABLED=true" \
    -e "ROBOTOPS_ROBOT_ID=e2e-test-robot" \
    -e "ROBOTOPS_ORG_ID=e2e-test-org" \
    -e "E2E_SETTLE_SECS=${SETTLE_SECS}" \
    ${FILTER_ENV[@]+"${FILTER_ENV[@]}"} \
    -v "${REPO_ROOT}:/workspace/src/rmw_robotops:ro" \
    -v "${OUTPUT_DIR}:/data" \
    -v "${SCRIPT_DIR}:/tests/scripts:ro" \
    rmw_robotops-e2e-combined:latest

log "Combined container finished."

# ── Step 5: Run query assertions ──────────────────────────────────────────────
log "Step 5: Running ROSQL assertion harness..."
docker run \
    --rm \
    -e "E2E_FILTER_MODE=${FILTER_MODE}" \
    -v "${OUTPUT_DIR}:/data:ro" \
    -v "${SCRIPT_DIR}:/tests/scripts:ro" \
    -v "${E2E_DIR}/queries.yml:/tests/queries.yml:ro" \
    rmw_robotops-e2e-query:latest \
    python3 /tests/scripts/assert_queries.py \
        --data-dir /data \
        --queries /tests/queries.yml

log "All assertions passed."
