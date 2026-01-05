# rmw_robotops development commands
# Install just: https://github.com/casey/just

# Default recipe - show available commands
default:
    @just --list

# Build the development Docker image
build:
    DOCKER_BUILDKIT=1 docker-compose build dev

# Build all Docker images (dev + test)
build-all:
    DOCKER_BUILDKIT=1 docker-compose build

# Start interactive development shell
dev:
    docker-compose run --rm dev

# Build the ROS2 package (inside container)
compile:
    docker-compose run --rm build

# Run all tests with sanitizers
test:
    docker-compose run --rm test

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

# Run linters
lint:
    docker-compose run --rm dev bash -c " \
        source /opt/ros/jazzy/setup.bash && \
        colcon build && \
        colcon test --packages-select rmw_robotops --ctest-args -R lint \
    "

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
