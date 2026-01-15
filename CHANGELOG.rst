^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Changelog for package rmw_robotops
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

0.5.0 (2026-01-15)
-------------------

* **NEW FEATURE**: Implemented diagnostics publishing to /robotops/diagnostics (ROB-128)
* Added DiagnosticsPublisher with background thread publishing DiagnosticsReport at configurable intervals (default: 10s)
* Implemented atomic metrics collection: traces_emitted, traces_dropped, auto_disabled counters
* Added configuration support: tracing.diagnostics.enabled and tracing.diagnostics.interval_secs
* Added environment variable overrides: ROBOTOPS_DIAGNOSTICS_ENABLED, ROBOTOPS_DIAGNOSTICS_INTERVAL_SECS
* Integrated with robot_agent's existing DiagnosticsSubscriber for health monitoring
* Diagnostics include: trace health, queue utilization, tracing state, RMW implementation
* Implemented Linux clock sync detection (PTP via ptp4l, NTP via chrony, systemd-timesyncd)
* Implemented Linux resource monitoring (/proc/self/status for RSS memory, /proc/self/task/{tid}/stat for thread CPU usage)
* Clock sync info cached for 60 seconds to minimize overhead
* All tests passing (13/13 including 5 linters)

0.4.0 (2026-01-13)
-------------------

* Added comprehensive integration tests with real RMW pub/sub to validate cross-process correlation
* Implemented 7 integration tests including ContentHashConsistency and CompleteCorrelationMetadata
* Fixed RMW lifecycle in tests - added proper rmw_shutdown() call before rmw_context_fini()
* Cleaned up misleading "fallback" terminology - content-based correlation is now primary/only method
* Added Documentation Maintenance section to CLAUDE.md for keeping ARCHITECTURE.md in sync
* Referenced ARCHITECTURE.md in README.md for better documentation discoverability
* All 214 tests passing (8 unit tests, 5 lint checks)

0.3.0 (2026-01-13)
-------------------

* **BREAKING**: Simplified to DDS-agnostic design - removed FastDDS-specific correlation strategy
* Implemented true content-based hashing using message introspection (walks full message structure)
* Correlation now uses: GID + timestamp + content_hash (works with any DDS)
* Reduced correlation_strategy.cpp from 499 to 140 lines (72% smaller, more maintainable)
* Updated COMPATIBILITY.md to reflect DDS-agnostic design and multi-DDS support

0.2.0 (2026-01-13)
-------------------

* Added COMPATIBILITY.md to track FastDDS version compatibility and breaking changes
* Implemented cross-process trace propagation infrastructure using FastDDS related_sample_identity
* Enhanced FastDDSCorrelationStrategy with trace context encoding/decoding (injection pending ROB-55)
* Added dependency tracking section to CLAUDE.md for monitoring upstream changes

0.1.15 (2026-01-12)
-------------------

* Fixed release workflows - added explicit upgrade of robotops packages before rosdep install to ensure latest versions are used
* Made just installation non-fatal in Dockerfile to avoid build failures if download server is unavailable

0.1.14 (2026-01-12)
-------------------

* Fixed release workflows - added rosdep install step to ensure build dependencies are satisfied before dpkg-buildpackage

0.1.13 (2026-01-12)
-------------------

* Upgraded robotops_msgs 0.3.2

0.1.12 (2026-01-12)
-------------------

* Enhanced release descriptions to include direct Cloudsmith package repository links for easier package access

0.1.11 (2026-01-12)
-------------------

* Fixed release workflows - corrected .deb package search path and added Cloudsmith repository links to release descriptions

0.1.10 (2026-01-12)
-------------------

* Refactored release workflows to use Docker-based builds for consistency with local development
* Removed manual ROS2/dependency installation in favor of pre-built Docker images
* Now using same Dockerfile as local ``just`` commands for CI/CD

0.1.9 (2026-01-12)
-------------------

* Fixed release workflows - added custom rosdep YAML to resolve robotops-config and robotops_msgs during bloom-generate
* Contributors: Kristoph Matthews, Claude Sonnet 4.5

0.1.8 (2026-01-12)
-------------------

* Fixed release workflows - corrected ROS2 APT repository path from ``/etc/os/sources.list.d/`` to ``/etc/apt/sources.list.d/``
* Fixed release workflows - added custom rosdep source for robotops-config and robotops_msgs dependencies
* Updated CLAUDE.md with mandatory pre-commit workflow (version bump before every commit)

0.1.7 (2026-01-12)
-------------------

* Fixed release workflows - removed invalid ``continue`` statement from awk script in changelog extraction
* Fixed release workflows - corrected path from ``/etc/os/sources.list.d/`` to ``/etc/apt/sources.list.d/``
* Contributors: Kristoph Matthews, Claude Sonnet 4.5

0.1.6 (2026-01-12)
-------------------

* Removed old cd.yml

0.1.5 (2026-01-12)
-------------------

* Implemented versioning and release automation strategy
* Added GitHub Actions workflows for production and development releases
* Added matrix builds for amd64 and arm64 architectures
* Enhanced version-check workflow to validate CHANGELOG.rst entries
* Added ``just bump-version`` command for version management (patch/minor/major)
* Updated README and CLAUDE.md with versioning policy and release process

0.1.0 (2025-01-05)
------------------
* Initial open source release
* RMW wrapper with distributed tracing support for ROS2 Jazzy
* OpenTelemetry-compatible trace context propagation via DDS metadata
* FastDDS property list integration for cross-process tracing
* Thread-local trace context management with zero-allocation hot path
* Lock-free bounded queue for trace event publishing
* Background publisher thread with best-effort QoS
* 8 safety guarantees ensuring robot operation is never compromised
* Support for amd64 and arm64 architectures
* Comprehensive test suite with AddressSanitizer and UBSan
* Performance benchmarks for latency and throughput validation
* Multi-DDS architecture ready for CycloneDDS and Connext support
* Contributors: Kristoph Matthews, Claude Sonnet 4.5
