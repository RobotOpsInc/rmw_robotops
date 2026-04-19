#!/usr/bin/env bash
# E2E orchestrator for demo-agent + ROSQL validation.
#
# Workflow:
#   1. Build rmw_robotops dev image and populate the colcon install-cache volume.
#   2. Build the demo-agent CI image (needed as the agent Dockerfile stage-1 base).
#   3. Build the three e2e images (publisher, agent, query).
#   4. Start the agent container in the background.
#   5. Run the publisher container to completion (scripted turtlesim scenario).
#   6. Wait a settle window, then SIGINT the agent to trigger final Parquet flush.
#   7. Run the rosql assertion harness.
#   8. Report and clean up.
#
# Options (env vars):
#   E2E_SETTLE_SECS        seconds to wait after publisher exits before SIGINT (default: 8)
#   E2E_ROS_DOMAIN_ID      ROS domain ID to isolate from host traffic (default: 42)
#   E2E_FILTER_MODE        set to 1 to run the topic-filter variant (default: 0)
#   E2E_KEEP_OUTPUT        set to 1 to skip cleanup of the Parquet output dir (default: 0)
#   E2E_OUTPUT_DIR         override the output directory (default: auto temp dir)
#   APT_REPO_URL           override the RobotOps APT repo (default: https://apt.robotops.com)

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

# Compose project name must match the top-level project so we share the named volume
COMPOSE_PROJECT="rmw_robotops"

OUTPUT_DIR="${E2E_OUTPUT_DIR:-}"
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="$(mktemp -d /tmp/e2e-rosql-XXXXXX)"
fi

AGENT_CONTAINER="e2e_agent_run"

log() { echo "[e2e] $*"; }

cleanup() {
    log "Cleaning up containers..."
    docker rm -f "$AGENT_CONTAINER" 2>/dev/null || true
    docker compose \
        -p e2e \
        -f "$E2E_DIR/docker-compose.e2e.yml" \
        down --remove-orphans 2>/dev/null || true

    if [ "$KEEP_OUTPUT" = "0" ] && [ -n "$OUTPUT_DIR" ]; then
        log "Removing output dir: $OUTPUT_DIR"
        rm -rf "$OUTPUT_DIR"
    else
        log "Parquet output preserved at: $OUTPUT_DIR"
    fi
}
trap cleanup EXIT

# ── Step 1: Build rmw_robotops dev image + populate install-cache volume ──────
log "Step 1: Building rmw_robotops dev image..."
docker compose \
    -p "$COMPOSE_PROJECT" \
    -f "$REPO_ROOT/docker-compose.yml" \
    build dev \
    --build-arg "APT_REPO_URL=${APT_REPO_URL}"

log "Step 1b: Running colcon build to populate install-cache volume..."
docker compose \
    -p "$COMPOSE_PROJECT" \
    -f "$REPO_ROOT/docker-compose.yml" \
    run --rm build

# ── Step 2: Build demo-agent CI image ────────────────────────────────────────
log "Step 2: Building robotops-demo-agent:ci image..."
docker build \
    -t robotops-demo-agent:ci \
    --build-arg "APT_REPO_URL=${APT_REPO_URL}" \
    "$DEMO_AGENT_DIR"

# ── Step 3: Build e2e images ──────────────────────────────────────────────────
log "Step 3: Building e2e images..."

docker build \
    -t rmw_robotops-e2e-publisher:latest \
    -f "$E2E_DIR/Dockerfile.publisher" \
    "$REPO_ROOT" \
    --build-arg "APT_REPO_URL=${APT_REPO_URL}"

docker build \
    -t rmw_robotops-e2e-agent:latest \
    -f "$E2E_DIR/Dockerfile.agent" \
    "$DEMO_AGENT_DIR" \
    --build-arg "APT_REPO_URL=${APT_REPO_URL}"

docker build \
    -t rmw_robotops-e2e-query:latest \
    -f "$E2E_DIR/Dockerfile.query" \
    "$E2E_DIR"

# ── Step 4: Start agent in background ────────────────────────────────────────
log "Step 4: Starting agent container..."
docker run \
    --detach \
    --name "$AGENT_CONTAINER" \
    --network host \
    -e "ROS_DOMAIN_ID=${ROS_DOMAIN_ID}" \
    -v "${OUTPUT_DIR}:/data" \
    rmw_robotops-e2e-agent:latest

# ── Step 5: Run publisher to completion ───────────────────────────────────────
log "Step 5: Running publisher (turtlesim scenario)..."

FILTER_ENV=()
if [ "$FILTER_MODE" = "1" ]; then
    log "  Filter mode: ROBOTOPS_TRACE_TOPIC_FILTER='^/turtle1/.*'"
    FILTER_ENV=(-e "ROBOTOPS_TRACE_TOPIC_FILTER=^/turtle1/.*")
fi

docker run \
    --rm \
    --network host \
    -e "ROS_DOMAIN_ID=${ROS_DOMAIN_ID}" \
    -e "RMW_IMPLEMENTATION=rmw_robotops" \
    -e "ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp" \
    -e "ROBOTOPS_TRACING_ENABLED=true" \
    -e "ROBOTOPS_ROBOT_ID=e2e-test-robot" \
    -e "ROBOTOPS_ORG_ID=e2e-test-org" \
    "${FILTER_ENV[@]}" \
    -v "${REPO_ROOT}:/workspace/src/rmw_robotops:ro" \
    -v "${COMPOSE_PROJECT}_install_cache:/workspace/install:ro" \
    rmw_robotops-e2e-publisher:latest

log "Publisher finished."

# ── Step 6: Settle, then SIGINT the agent ────────────────────────────────────
log "Step 6: Waiting ${SETTLE_SECS}s for correlation window + final events..."
sleep "$SETTLE_SECS"

log "  Sending SIGINT to agent (triggers final Parquet flush)..."
docker kill --signal SIGINT "$AGENT_CONTAINER" || true

log "  Waiting for agent to flush and exit..."
docker wait "$AGENT_CONTAINER" || true

# ── Step 7: Run query assertions ──────────────────────────────────────────────
log "Step 7: Running ROSQL assertion harness..."
docker run \
    --rm \
    -e "E2E_FILTER_MODE=${FILTER_MODE}" \
    -v "${OUTPUT_DIR}:/data:ro" \
    -v "${E2E_DIR}/scripts:/tests/scripts:ro" \
    -v "${E2E_DIR}/queries.yml:/tests/queries.yml:ro" \
    rmw_robotops-e2e-query:latest \
    python3 /tests/scripts/assert_queries.py \
        --data-dir /data \
        --queries /tests/queries.yml

log "All assertions passed."
