# Claude Code Rules for rmw_robotops

This file documents project-specific rules and conventions for AI-assisted development.

## Git Workflow

### Branching Strategy

- **`main`** - Production environment only
- **`development`** - Active development branch (default base)
- **Feature branches** - Cut from `development`, NOT `main`

### Branch Naming

Feature branches MUST be named according to the issue's `gitBranchName` field:

```bash
# Example from Linear issue ROB-55:
git checkout development
git checkout -b feature/rob-55-rmw_robotops-custom-rmw-implementation-for-distributed
```

### Pull Requests

- **Base branch**: Always `development` (NOT `main`)
- **Merge to `main`**: Only via release process

Example:
```bash
# ✅ Correct
gh pr create --base development --head feature/rob-55-...

# ❌ Wrong
gh pr create --base main --head feature/rob-55-...
```

## Code Standards

### Safety-Critical Guidelines

This is a **safety-critical** ROS2 middleware layer. Code must adhere to:

1. **No allocations in hot path** - Use pre-allocated thread-local buffers
2. **No exceptions propagate** - All trace functions must be `noexcept`
3. **Real messages first** - Trace emission happens AFTER real message delivery
4. **Lock-free where possible** - Thread-local storage, atomic operations only
5. **Best-effort tracing** - Failures must not crash or block robot operation

### Performance Requirements

- **Latency**: < 1µs median added overhead per message
- **CPU**: < 5% overhead vs underlying RMW
- **Memory**: Zero allocations during message interception

### Testing Requirements

All code changes must include:

- **Unit tests** - For isolated components
- **Safety tests** - AddressSanitizer/UBSan clean
- **Integration tests** - Context propagation verification
- **Performance benchmarks** - Latency/throughput validation

**CRITICAL: Always run CI locally before committing:**
```bash
# Run the full CI suite locally to replicate GitHub Actions
just ci

# This runs:
# - just ci-lint (all lint checks)
# - just ci-test (all tests with sanitizers)
# Both will run even if one fails, matching CI behavior
```

Safety tests MUST pass before merging:
```bash
colcon test --packages-select rmw_robotops --ctest-args -R test_safety
```

## Docker Development

### Environment

**CRITICAL**: This project is developed on macOS where ROS2 is not installed natively.

**ALL ROS2 commands, builds, and tests MUST be executed inside Docker containers.**

Never attempt to run:
- `colcon build`
- `colcon test`
- `ros2` commands
- `source /opt/ros/*/setup.bash`
- Any ROS2-related operations

...directly on the host macOS system. Always use Docker:

```bash
# Correct: Run commands in Docker
docker-compose run --rm dev bash -c "colcon build ..."
docker-compose run --rm test bash -c "colcon test ..."

# Wrong: Run commands on macOS
colcon build  # This will fail - ROS2 not installed!
```

### Multi-Stage Dockerfile

Single `Dockerfile` with build stages:
- `base` - Common dependencies (ROS2, FastDDS, Cloudsmith, robotops_msgs)
- `dev` - Development environment (extends base)
- `test` - Test environment with sanitizers (extends base)

This keeps the setup DRY and maintainable.

### Cloudsmith Authentication

Private `robotops_msgs` dependency requires Cloudsmith credentials:

```bash
# Copy template and configure
cp .env.local.template .env.local
# Edit .env.local with your credentials:
# CLOUDSMITH_USERNAME=your-username
# CLOUDSMITH_API_KEY=your-api-key
```

The `.env.local` file is automatically loaded by `docker-compose` and must **never be committed** (see `.gitignore`).

Each developer uses their own Cloudsmith username and API key.

### Viewing robotops_msgs Schemas

To view the actual .msg definitions from the installed package:

```bash
# Find message files
docker-compose run --rm dev bash -c "find /opt/ros/jazzy -name '*.msg' -path '*robotops_msgs*'"

# View TraceEvent.msg
docker-compose run --rm dev bash -c "cat /opt/ros/jazzy/share/robotops_msgs/msg/TraceEvent.msg"

# View TraceContextChange.msg
docker-compose run --rm dev bash -c "cat /opt/ros/jazzy/share/robotops_msgs/msg/TraceContextChange.msg"
```

This is helpful when implementing code that needs to match the exact message schema.

## Architecture Decisions

### DDS Metadata Propagation

**Chosen approach**: DDS Property List (Option 4)

- Primary: FastDDS property list API for trace context
- Fallback: No-op mode if properties unavailable
- Rationale: Per-message metadata without payload modification

See commit history for full analysis of alternatives.

### Underlying RMW

**Primary target**: FastDDS (`rmw_fastrtps_cpp`)

Multi-DDS support (CycloneDDS, etc.) tracked in ROB-105.

## Related Issues

- **ROB-55**: This implementation (rmw_robotops)
- **ROB-54**: robotops_msgs package
- **ROB-33**: Distributed Tracing epic (parent)
- **ROB-105**: Multi-DDS testing

## Commit Message Format

Follow conventional commits:

```
feat(component): Brief description

Detailed explanation of changes and rationale.

**Key changes:**
- Bullet points for major items

Related to ROB-XX

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

## Notes for Future Contributors

- This package runs **inside every robot node process** - bugs here can crash robots
- Always prioritize safety guarantees over features
- When in doubt, fail safe (disable tracing rather than risk blocking messages)
