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

## Versioning and Releases

### Version Management

The `package.xml` file is the source of truth for versioning. Use the `bump-version` command:

```bash
# For bug fixes (0.1.4 → 0.1.5)
just bump-version patch

# For new features (0.1.4 → 0.2.0)
just bump-version minor

# For breaking changes (0.1.4 → 1.0.0) - COORDINATE WITH TEAM!
just bump-version major
```

Each command:
- Updates version in `package.xml`
- Creates a new entry in `CHANGELOG.rst` with today's date
- Prompts you to fill in the changelog

**After bumping:**
1. Edit `CHANGELOG.rst` to document your changes
2. Commit both `package.xml` and `CHANGELOG.rst`
3. Create a PR to `development`

### Version Alignment

**Major versions** must align across the RobotOps ecosystem:
- `robotops_config`
- `robotops_msgs`
- `rmw_robotops` (this repo)
- `robot_agent`

**Minor and patch versions** evolve independently within the same major version. Backward compatibility is maintained.

### Release Process

**DO NOT create releases manually.** Use GitHub Actions workflows:

#### Production Release (from `main` branch)

1. Merge PR from `development` to `main`
2. Go to **Actions** → **Release** in GitHub UI
3. Click **Run workflow** on `main` branch
4. Workflow:
   - Creates git tag `v{version}` (e.g., `v0.1.5`)
   - Builds Debian packages for amd64 and arm64
   - Publishes to AWS S3 via Aptly at https://apt.robotops.com
   - Creates GitHub Release with changelog excerpt

#### Development Release (from `development` branch)

1. Go to **Actions** → **Release Development** in GitHub UI
2. Click **Run workflow** on `development` branch
3. Workflow:
   - Creates git tag `v{version}-development-{sha}` (e.g., `v0.1.5-development-abc123`)
   - Builds Debian packages for amd64 and arm64
   - Publishes to AWS S3 via Aptly at https://apt.development.robotops.com
   - Creates GitHub Pre-Release with changelog excerpt

**Both workflows support dry-run mode** for validation before actual release.

### Version Check CI

Pull requests to `main` or `development` automatically verify:
- Version in `package.xml` has been incremented
- CHANGELOG.rst has an entry for the new version (format: `X.Y.Z (YYYY-MM-DD)`)
- New version is higher than latest published version in APT repository

If these checks fail, update your version and changelog before merging.

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
- `base` - Common dependencies (ROS2, FastDDS, APT repository, robotops_msgs)
- `dev` - Development environment (extends base)
- `test` - Test environment with sanitizers (extends base)

This keeps the setup DRY and maintainable.

### APT Repository Configuration

RobotOps packages (`robotops_msgs`, `robotops_config`) are hosted on AWS S3 and served via public APT repositories:

- **Production**: https://apt.robotops.com (default)
- **Development**: https://apt.development.robotops.com

The production repository is used by default. To use the development repository:

```bash
# Copy template and configure
cp .env.local.template .env.local
# Edit .env.local to use development repository:
# APT_REPO_URL=https://apt.development.robotops.com
```

The `.env.local` file is automatically loaded by `docker-compose` and must **never be committed** (see `.gitignore`).

No authentication is required - all packages are publicly accessible.

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

### Trace Correlation Strategy

**Chosen approach**: DDS-agnostic content-based correlation

rmw_robotops uses only standard RMW APIs - no DDS-specific features required.

**Correlation metadata:**
- Publisher GID (24-byte unique identifier from `rmw_message_info_t`)
- Source timestamp (nanosecond precision from `rmw_message_info_t`)
- Content hash (FNV-1a hash of full message structure via introspection)

**Key design principles:**
- Works with any DDS implementation (FastDDS, CycloneDDS, Connext, etc.)
- No payload modification - passive observation only
- Post-hoc correlation by robot_agent using metadata
- Intra-process uses thread-local storage (TLS) for direct context propagation

See commit history (v0.3.0+) for implementation details.

### DDS Independence

rmw_robotops is **fully DDS-agnostic**. It works identically with:
- FastDDS (`rmw_fastrtps_cpp`) - default on ROS2 Jazzy
- CycloneDDS (`rmw_cyclonedds_cpp`)
- Connext DDS (`rmw_connextdds`)
- Any future RMW implementation

No DDS-specific code paths or feature detection required.

## Related Issues

- **ROB-55**: This implementation (rmw_robotops)
- **ROB-54**: robotops_msgs package
- **ROB-33**: Distributed Tracing epic (parent)
- **ROB-105**: Multi-DDS testing

## Pre-Commit Workflow

**CRITICAL: Before EVERY commit, you MUST:**

1. **Bump the version** (unless it's already been bumped for this change):
   ```bash
   just bump-version patch  # For most changes
   just bump-version minor  # For new features
   just bump-version major  # For breaking changes (coordinate with team)
   ```

2. **Update CHANGELOG.rst** with your changes - the bump-version command creates the entry, you fill in the details

3. **Stage all changes including package.xml and CHANGELOG.rst**:
   ```bash
   git add -A
   ```

4. **Then commit** with conventional commit message

**This applies to ALL commits, including:**
- Bug fixes
- Workflow fixes
- Documentation updates
- Any code changes

The only exception is if you're making multiple related changes in quick succession and want to bundle them into a single version bump - but this should be rare.

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

## Dependency Tracking

### Regular Monitoring

When working on this codebase, periodically check for upstream changes that may affect compatibility:

1. **FastDDS releases**: https://github.com/eProsima/Fast-DDS/releases
   - Watch for breaking changes in RTPS APIs we use (WriteParams, SampleIdentity, SampleInfo)
   - Review migration guides for major version transitions

2. **rmw_fastrtps updates**: https://github.com/ros2/rmw_fastrtps
   - Track PRs affecting FastDDS version requirements
   - Monitor Jazzy branch for backports

3. **ROS2 distribution releases**: https://docs.ros.org/en/rolling/Releases.html
   - Check which FastDDS version ships with each distribution
   - Plan compatibility work before new distributions release

### Version Boundaries

See `COMPATIBILITY.md` for current supported versions. Key boundaries:

- **FastDDS 2.x → 3.x**: Major breaking change (namespace, headers, some APIs)
- **Within FastDDS 2.x**: API compatible, may need recompilation

### When to Update

- Before adding support for a new ROS2 distribution
- When upstream reports security vulnerabilities
- When users report compatibility issues

## Documentation Maintenance

### Architecture Documentation

**CRITICAL: Keep ARCHITECTURE.md in sync with code changes.**

When making significant changes, update `ARCHITECTURE.md` if they affect:

- **Component responsibilities** - New components or changed roles
- **Data flows** - Modified publish/subscribe/service paths
- **Safety guarantees** - Changed safety contracts or implementation
- **Trace context propagation** - Intra-process or cross-process correlation changes
- **Thread safety model** - Locking, atomics, or concurrency patterns
- **Edge cases** - New failure modes or behaviors
- **Performance characteristics** - Changed overhead or resource usage

**Examples requiring architecture doc updates:**
- Adding new RMW interception points (e.g., actions, lifecycle)
- Changing correlation strategy or metadata
- Modifying queue behavior or background thread
- Adding/removing safety guarantees
- Changing thread-local storage usage

**Minor changes NOT requiring updates:**
- Bug fixes that don't change behavior
- Performance optimizations within existing design
- Code refactoring without functional changes
- Documentation/comment improvements

**Process:**
1. Make code changes
2. Update ARCHITECTURE.md if criteria above apply
3. Include architecture doc changes in the same commit
4. Reference doc updates in commit message

## Notes for Future Contributors

- This package runs **inside every robot node process** - bugs here can crash robots
- Always prioritize safety guarantees over features
- When in doubt, fail safe (disable tracing rather than risk blocking messages)
