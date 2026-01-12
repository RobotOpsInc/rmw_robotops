# rmw_robotops development commands
# Install just: https://github.com/casey/just

# Load credentials from .env.local
set dotenv-load
set dotenv-filename := ".env.local"

# Configuration - can be overridden via environment variables
## Development:
# export CLOUDSMITH_REPO := env_var_or_default('CLOUDSMITH_REPO', 'robotops-development')
export CLOUDSMITH_USERNAME := env_var_or_default('CLOUDSMITH_USERNAME', '')
export CLOUDSMITH_API_KEY := env_var_or_default('CLOUDSMITH_API_KEY', '')
## Production:
export CLOUDSMITH_REPO := env_var_or_default('CLOUDSMITH_REPO', 'robotops')

# Default recipe - show available commands
default:
    @just --list

# Build the development Docker image
build repo=CLOUDSMITH_REPO:
    CLOUDSMITH_REPO={{repo}} CLOUDSMITH_USERNAME={{CLOUDSMITH_USERNAME}} CLOUDSMITH_API_KEY={{CLOUDSMITH_API_KEY}} DOCKER_BUILDKIT=1 docker-compose build dev

# Build all Docker images (dev + test)
build-all repo=CLOUDSMITH_REPO:
    CLOUDSMITH_REPO={{repo}} CLOUDSMITH_USERNAME={{CLOUDSMITH_USERNAME}} CLOUDSMITH_API_KEY={{CLOUDSMITH_API_KEY}} DOCKER_BUILDKIT=1 docker-compose build

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
    @if [ -z "{{CLOUDSMITH_USERNAME}}" ]; then \
        echo "❌ CLOUDSMITH_USERNAME not set (check .env.local)"; \
    elif [ -z "{{CLOUDSMITH_API_KEY}}" ]; then \
        echo "❌ CLOUDSMITH_API_KEY not set (check .env.local)"; \
    else \
        echo "✅ Cloudsmith credentials configured"; \
        echo "   Username: {{CLOUDSMITH_USERNAME}}"; \
        echo "   API Key: <redacted>"; \
    fi
    @echo ""
    @if [ -f .env.local ]; then \
        echo "✅ .env.local file found"; \
    else \
        echo "❌ .env.local file missing (copy from .env.local.template)"; \
    fi
    @echo ""
    @echo "Testing Docker build..."
    @CLOUDSMITH_REPO={{CLOUDSMITH_REPO}} CLOUDSMITH_USERNAME={{CLOUDSMITH_USERNAME}} CLOUDSMITH_API_KEY={{CLOUDSMITH_API_KEY}} DOCKER_BUILDKIT=1 docker-compose build dev > /dev/null 2>&1 && echo "✅ Docker build successful" || echo "❌ Docker build failed"

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
    -docker-compose run --rm test bash -c "just ci-benchmark"
    @echo "✅ CI suite completed!"

# Run full CI suite (for GitHub Actions, already in container)
ci-inner:
    #!/usr/bin/env bash
    set -exo pipefail
    echo "🚀 Running CI suite (in container)..."
    just ci-lint
    just ci-test
    just ci-benchmark || true  # Benchmark is best-effort
    echo "✅ CI suite completed!"
