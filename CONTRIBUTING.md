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

## Development Setup

### Prerequisites

- ROS 2 Jazzy
- Docker (for containerized development)
- C++17 compatible compiler

### Building

```bash
# Using Docker (recommended)
docker build --target dev -t rmw_robotops:dev .

# Or locally with colcon
source /opt/ros/jazzy/setup.bash
colcon build --packages-select rmw_robotops
```

### Running Tests

```bash
# Lint checks
colcon test --packages-select rmw_robotops --ctest-args -R lint

# All tests
colcon test --packages-select rmw_robotops

# View results
colcon test-result --verbose
```

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

## Documentation

- Update README.md for user-facing changes
- Add inline documentation for complex logic
- Update STATUS.md for known issues or limitations
- Include usage examples for new features

## Questions or Issues?

- Check existing [GitHub Issues](https://github.com/robotops/rmw_robotops/issues)
- Create a new issue with detailed description
- For security issues, please contact maintainers directly

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.
