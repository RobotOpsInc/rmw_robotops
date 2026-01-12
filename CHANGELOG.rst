^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Changelog for package rmw_robotops
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

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
