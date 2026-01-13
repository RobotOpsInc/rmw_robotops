^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Changelog for package rmw_robotops
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

0.1.15 (2026-01-12)
-------------------

* Fixed release workflows - added explicit upgrade of robotops packages before rosdep install to ensure latest versions are used
* Moved just installation from base to dev stage only in Dockerfile (not needed for release builds)

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
