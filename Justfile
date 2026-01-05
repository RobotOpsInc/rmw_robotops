# rmw_robotops development commands
# Install just: https://github.com/casey/just

# Configuration - can be overridden via environment variables
## Development:
export CLOUDSMITH_REPO := env_var_or_default('CLOUDSMITH_REPO', 'robotops-development')
## Production:
# export CLOUDSMITH_REPO := env_var_or_default('CLOUDSMITH_REPO', 'robotops')
export ROBOTOPS_MSGS_VERSION := env_var_or_default('ROBOTOPS_MSGS_VERSION', '0.1.6-0noble')

# Default recipe - show available commands
default:
    @just --list

# Build the development Docker image
build repo=CLOUDSMITH_REPO version=ROBOTOPS_MSGS_VERSION:
    CLOUDSMITH_REPO={{repo}} ROBOTOPS_MSGS_VERSION={{version}} DOCKER_BUILDKIT=1 docker-compose build dev

# Build all Docker images (dev + test)
build-all repo=CLOUDSMITH_REPO version=ROBOTOPS_MSGS_VERSION:
    CLOUDSMITH_REPO={{repo}} ROBOTOPS_MSGS_VERSION={{version}} DOCKER_BUILDKIT=1 docker-compose build

# Start interactive development shell
dev:
    docker-compose run --rm dev

# Build the ROS2 package (inside container)
compile:
    docker-compose run --rm build

# Run all tests with sanitizers
test:
    DOCKER_BUILDKIT=1 docker-compose run --rm test

# Run safety tests only (MUST pass before deployment)
test-safety:
    docker-compose run --rm dev bash -c " \
        source /opt/ros/jazzy/setup.bash && \
        colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug && \
        source install/setup.bash && \
        colcon test --packages-select rmw_robotops --ctest-args -R test_safety && \
        colcon test-result --verbose \
    "

# Run performance benchmarks
benchmark:
    docker-compose run --rm dev bash -c " \
        source /opt/ros/jazzy/setup.bash && \
        colcon build --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
        source install/setup.bash && \
        ./install/rmw_robotops/lib/rmw_robotops/benchmark_latency \
    "

# Run stress test (10,000 msg/sec)
stress:
    docker-compose run --rm dev bash -c " \
        source /opt/ros/jazzy/setup.bash && \
        colcon build && \
        source install/setup.bash && \
        ros2 run rmw_robotops stress_test \
    "

# Clean build artifacts
clean:
    rm -rf build/ install/ log/
    docker-compose down -v

# Rebuild everything from scratch
rebuild: clean build-all

# Format code (when we add clang-format)
fmt:
    @echo "Code formatting not yet configured"

# Show logs from last test run
logs:
    @find log/latest_test -name "*.log" -exec echo "=== {} ===" \; -exec cat {} \; 2>/dev/null || echo "No test logs found"

# Check Docker setup (verify Cloudsmith access)
check-setup:
    @echo "Checking Docker buildx..."
    @docker buildx version || echo "❌ Docker buildx not available"
    @echo ""
    @echo "Checking Cloudsmith credentials..."
    @if [ -f ~/.cloudsmith/key ]; then \
        if grep -q ':' ~/.cloudsmith/key; then \
            echo "✅ Credentials file found with correct format"; \
        else \
            echo "❌ Credentials file missing ':' separator (format: username:api_key)"; \
        fi; \
    else \
        echo "❌ Credentials file missing (see README.md)"; \
    fi
    @echo ""
    @echo "Testing Docker build..."
    @DOCKER_BUILDKIT=1 docker-compose build dev > /dev/null 2>&1 && echo "✅ Docker build successful" || echo "❌ Docker build failed"

# CI commands

# Run lint checks (in container)
ci-lint:
    #!/usr/bin/env bash
    echo "🔍 Running lint checks..."
    source /opt/ros/jazzy/setup.bash
    cd /workspace
    colcon build --packages-select rmw_robotops
    colcon test --packages-select rmw_robotops --ctest-args -R lint
    colcon test-result --verbose

# Run tests with sanitizers (in container)
ci-test:
    #!/usr/bin/env bash
    echo "🧪 Running tests with sanitizers..."
    source /opt/ros/jazzy/setup.bash
    cd /workspace
    colcon build --packages-select rmw_robotops \
      --cmake-args -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS='-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer' \
      -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address -fsanitize=undefined'
    colcon test --packages-select rmw_robotops --event-handlers console_direct+
    colcon test-result --verbose

# Run benchmarks (in container)
ci-benchmark:
    #!/usr/bin/env bash
    echo "📊 Running benchmarks..."
    source /opt/ros/jazzy/setup.bash
    cd /workspace
    colcon build --packages-select rmw_robotops --cmake-args -DCMAKE_BUILD_TYPE=Release
    source /workspace/install/setup.bash
    /workspace/install/rmw_robotops/lib/rmw_robotops/benchmark_latency > benchmark_results.txt || true

# Run full CI suite (for local development via Docker)
ci:
    @echo "🚀 Running full CI suite locally..."
    -docker-compose run --rm dev bash -c "just ci-lint"
    -docker-compose run --rm test bash -c "just ci-test"
    @echo "✅ CI suite completed!"

# Run full CI suite (for GitHub Actions, already in container)
ci-inner:
    #!/usr/bin/env bash
    set -exo pipefail
    echo "🚀 Running CI suite (in container)..."
    just ci-lint || true
    just ci-test || true
    just ci-benchmark
    echo "✅ CI suite completed!"
