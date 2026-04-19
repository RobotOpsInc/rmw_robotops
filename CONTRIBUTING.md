<!--
Copyright 2025 Robot Ops Inc.
SPDX-License-Identifier: Apache-2.0
-->

# Contributing to rmw_robotops

Thank you for your interest in contributing to rmw_robotops! This document provides guidelines for contributing to the project.

## Code of Conduct

Please be respectful and considerate in all interactions with the project and its community.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/your-username/rmw_robotops.git`
3. Create a feature branch: `git checkout -b feature/your-feature-name`

## Prerequisites

**Batteries included!** Only one thing needed on your host machine:

1. **Docker** with buildx support
   - macOS: Docker Desktop (buildx included by default)
   - Linux: `docker buildx install`

Everything else runs in containers — no ROS2, no Ubuntu required on host! RobotOps packages are fetched from the public APT repository (`apt.robotops.com`) — no credentials required.

## Quick Start

### 1. Install just (task runner)

```bash
# macOS
brew install just

# Linux
curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to ~/bin
```

### 2. Build and develop

```bash
# See all available commands
just

# Build development image
just build

# Start interactive shell
just dev

# Inside container, build the package:
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install
```

## Common Tasks

All tasks use `just` commands. Run `just` to see all available commands.

### Development

```bash
just dev          # Interactive development shell
just compile      # Build the package
just clean        # Remove build artifacts
just rebuild      # Clean + rebuild from scratch
```

### Testing

```bash
just test         # Run all tests with sanitizers
just test-safety  # Run safety tests (MUST pass before deployment)
just benchmark    # Performance benchmarks
just stress       # Stress test (10,000 msg/sec)
just logs         # Show logs from last test run
```

**Performance Requirements:**
- Median latency: < 1µs added overhead
- CPU overhead: < 5% vs underlying RMW
- Memory: Zero allocations in hot path

### CI/CD

```bash
# Run the exact same CI suite that runs in GitHub Actions
just ci

# This executes:
# 1. just ci-lint - All lint checks (copyright, cpplint, uncrustify, etc.)
# 2. just ci-test - All tests with AddressSanitizer and UBSan
# Both steps run even if one fails, matching GitHub Actions behavior

# Expected results (as of 2026-01-05):
# - 141 tests total (32 lint + 109 functional/performance)
# - 29 tests skipped
# - 0 functional failures ✅
# - 4-5 performance test failures (need tuning for CI environment)
#
# Note: CI currently fails due to performance tests. These need threshold
# adjustments to pass in containerized CI environments.
```

**IMPORTANT:** Always run `just ci` before pushing to catch issues early!

### Code Quality

```bash
just lint         # Run linters
just fmt          # Format code
```

### Setup Verification

```bash
just check-setup  # Verify Docker and build setup
```

## Development Workflow

### Making Changes

1. **Create a feature branch** from `development`:
   ```bash
   git checkout development
   git pull origin development
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes** and ensure tests pass:
   ```bash
   just ci  # Run full CI suite locally
   ```

3. **Bump the version** (if needed):
   ```bash
   # For bug fixes (0.1.4 → 0.1.5)
   just bump-version patch

   # For new features (0.1.4 → 0.2.0)
   just bump-version minor

   # For breaking changes (0.1.4 → 1.0.0) - coordinate with team!
   just bump-version major
   ```

4. **Edit CHANGELOG.rst** with your changes:
   ```rst
   0.1.5 (2026-01-12)
   ------------------

   * Fixed memory leak in trace context cleanup
   * Added support for custom trace sampling rates
   ```

5. **Create a pull request** to `development`:
   ```bash
   git add .
   git commit -m "feat: your changes"
   git push origin feature/your-feature-name
   gh pr create --base development
   ```

### Local Development (Mac)

All development happens in Docker containers since ROS2 Jazzy doesn't run natively on macOS.

```bash
# Start dev container
just dev

# Inside container: make changes, rebuild
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Debug

# Run tests
colcon test --event-handlers console_direct+

# Or from outside container
just compile  # Build
just test     # Test with sanitizers
```

### Adding New Tests

1. Create test file in `test/`
2. Add to `CMakeLists.txt` using `ament_add_gtest()`
3. Run: `just test`

### Creating Releases

**Production Release** (from `main` branch):
1. Merge changes from `development` to `main`
2. Navigate to **Actions** → **Release** in GitHub
3. Click "Run workflow" on `main` branch
4. Packages published to `https://apt.robotops.com`

**Development Release** (from `development` branch):
1. Navigate to **Actions** → **Release Development** in GitHub
2. Click "Run workflow" on `development` branch
3. Packages published to `https://apt.development.robotops.com`

Both workflows:
- Create git tags (`v0.1.5` for production, `v0.1.5-development-abc123` for dev)
- Build for amd64 and arm64 architectures
- Publish Debian packages to the APT repository
- Extract release notes from CHANGELOG.rst

**Dry Run:** Both workflows support dry-run mode to validate before creating releases.

## Code Style

This project follows ROS 2 coding standards:

- C++ code should follow the [ROS 2 C++ Style Guide](https://docs.ros.org/en/rolling/Contributing/Code-Style-Language-Versions.html)
- Python code should follow PEP 8
- All files must include the Apache 2.0 license header

### Running Linters

```bash
# C++ files (automatic via colcon test)
colcon test --packages-select rmw_robotops --ctest-args -R lint

# Or run full CI suite
just ci
```

## Pull Request Process

1. Ensure all tests pass locally
2. Update documentation if needed
3. Add tests for new functionality
4. Follow the semantic versioning scheme for version updates
5. Create a pull request with a clear description of changes

### PR Guidelines

- Keep changes focused and atomic
- Write clear commit messages
- Reference related issues
- Ensure CI passes before requesting review

## Commit Message Format

Follow conventional commit format:

```
<type>: <description>

[optional body]

[optional footer]
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, etc.)
- `refactor`: Code refactoring
- `test`: Adding or updating tests
- `chore`: Maintenance tasks

Example:
```
feat: Add support for custom QoS profiles

Implements configurable QoS settings via environment variables.
Includes tests and documentation updates.

Closes #123
```

## Versioning

This project uses [Semantic Versioning](https://semver.org/):

- MAJOR: Incompatible API changes
- MINOR: Backwards-compatible functionality additions
- PATCH: Backwards-compatible bug fixes

Version must be updated in `package.xml` and should be greater than the latest published version.

## CI/CD

### Continuous Integration

All pull requests must pass CI checks:

- Lint checks (ament_cpplint, ament_flake8, ament_copyright)
- Build validation
- Unit tests with sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer)
- Benchmarks

### Continuous Deployment

Merges to `development` or `main` branches trigger:

- Multi-architecture builds (amd64, arm64)
- Debian package generation
- Deployment to Cloudsmith (robotops-development or robotops repository)

## Testing

### Types of Tests

1. **Unit Tests**: Test individual components in isolation
2. **Integration Tests**: Test component interactions
3. **Safety Tests**: Verify kill switch and safety mechanisms
4. **Performance Tests**: Benchmark critical paths

### Writing Tests

Tests use Google Test framework:

```cpp
#include <gtest/gtest.h>

TEST(ComponentTest, BasicFunctionality)
{
  // Arrange
  Component component;

  // Act
  auto result = component.do_something();

  // Assert
  EXPECT_EQ(result, expected_value);
}
```

## Troubleshooting

### Quick Diagnosis

```bash
just check-setup  # Checks Docker and build
```

### Build Fails with Missing robotops_msgs

The package depends on `robotops_msgs` from `apt.robotops.com`. Run `just check-setup` to diagnose:
1. Docker buildx available
2. Network access to `apt.robotops.com`
3. Build succeeds

### Tests Fail with AddressSanitizer

This is expected during development. Safety tests with ASan are designed to catch memory issues early.

```bash
just test  # Runs with detailed sanitizer output
```

## Documentation

- Update README.md for user-facing changes
- Update ARCHITECTURE.md for structural/design changes
- Add inline documentation for complex logic
- Include usage examples for new features

## Questions or Issues?

- Check existing [GitHub Issues](https://github.com/robotops/rmw_robotops/issues)
- Create a new issue with detailed description
- For security issues, please contact maintainers directly

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.
