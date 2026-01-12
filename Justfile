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

# Version Management

# Bump version (usage: just bump-version patch|minor|major)
bump-version type:
    #!/usr/bin/env bash
    set -euo pipefail

    if [[ "{{type}}" != "patch" && "{{type}}" != "minor" && "{{type}}" != "major" ]]; then
        echo "Error: type must be 'patch', 'minor', or 'major'"
        exit 1
    fi

    CURRENT=$(grep -oP '(?<=<version>)[^<]+' package.xml)
    IFS='.' read -r major minor patch <<< "$CURRENT"
    DATE=$(date +%Y-%m-%d)

    # Calculate new version
    case "{{type}}" in
        patch)
            NEW_VERSION="$major.$minor.$((patch + 1))"
            ;;
        minor)
            NEW_VERSION="$major.$((minor + 1)).0"
            ;;
        major)
            NEW_VERSION="$((major + 1)).0.0"
            echo "⚠️  MAJOR VERSION BUMP: $CURRENT -> $NEW_VERSION"
            echo "⚠️  Remember: Major versions must align across all RobotOps components!"
            echo "   - robotops_config"
            echo "   - robotops_msgs"
            echo "   - rmw_robotops"
            echo "   - robot_agent"
            ;;
    esac

    echo "Bumping version: $CURRENT -> $NEW_VERSION"

    # Update package.xml
    sed -i.bak "s|<version>$CURRENT</version>|<version>$NEW_VERSION</version>|" package.xml
    rm package.xml.bak

    # Add changelog entry
    cat > /tmp/changelog_entry.txt << EOF
$NEW_VERSION ($DATE)
-------------------

*

EOF

    # Insert at the top of CHANGELOG.rst (after the header)
    awk '/^[0-9]+\.[0-9]+\.[0-9]+ \(/ { if (!inserted) { system("cat /tmp/changelog_entry.txt"); inserted=1 } } { print }' CHANGELOG.rst > /tmp/CHANGELOG.rst.new
    mv /tmp/CHANGELOG.rst.new CHANGELOG.rst
    rm /tmp/changelog_entry.txt

    echo "✅ Version bumped to $NEW_VERSION"
    echo "📝 Edit CHANGELOG.rst to add your changes"
    [[ "{{type}}" == "major" ]] && echo "⚠️  Coordinate with other RobotOps repos for aligned major version bump!"

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
