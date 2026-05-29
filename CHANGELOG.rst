^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Changelog for package rmw_robotops
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

0.9.6 (2026-05-29)
-------------------

* ``CONTRIBUTING.md`` / ``README.md`` / ``CLAUDE.md``: add the Generative AI contributions policy and remind contributors to review AI-assisted work before submission.

0.9.5 (2026-04-19)
-------------------

* ``README.md``: add ``RMW_IMPLEMENTATION`` / ``ROBOTOPS_UNDERLYING_RMW`` env vars to the demo agent quickstart so users know how to activate rmw_robotops before running a node.
* ``README.md``: move Usage section before FAQ and Benchmarks for better reading order.

0.9.4 (2026-04-19)
-------------------

* ``README.md``: fix rosdep install instructions — the old step 2 wrote a raw YAML to ``/etc/ros/rosdep/sources.list.d/``, which ``rosdep`` silently ignores (it only ingests YAML files referenced from ``.list`` entries). ``rosdep install`` failed with "no rosdep rule for [rmw_robotops|robotops_msgs|robotops-config]".
* ``rosdep/robotops.yaml``: new — ships the rosdep key mappings in-repo so downstream users can register them via a single ``.list`` entry pointing at the raw GitHub URL. Adds the previously missing ``rmw_robotops`` key (step 3 instructs users to ``<depend>rmw_robotops</depend>``, so rosdep must know how to resolve it).

0.9.3 (2026-04-19)
-------------------

* ``rmw_service.cpp``: fix ``EVENT_SERVICE_RESPONSE`` to reuse the span_id from ``EVENT_SERVICE_REQUEST`` (stored in thread-local context) so ``span_reconstructor`` can correlate them into a single SERVER span. Previously both events generated independent span_ids, yielding 0 SERVER spans in Parquet.
* ``demo-agent/tests/e2e``: switch from two-container (publisher + agent) to single combined container; fixes DDS multicast failure in Docker Desktop for macOS. Resolves all 15 e2e ROSQL query assertions.
* ``demo-agent/src/export/parquet.rs``: replace nested Tokio runtime with ``tokio::task::block_in_place`` to avoid panic on first Parquet flush.
* ``demo-agent/src/subscribers``: fix QoS mismatch — trace and context subscribers now use ``best_effort()`` to match rmw_robotops publisher QoS.
* ``demo-agent/tests/e2e/Dockerfile.query``: use ``ubuntu:24.04`` (GLIBC 2.39) instead of ``debian:bookworm-slim`` (GLIBC 2.36) to satisfy ``rosql`` GLIBC >= 2.38 requirement.

0.9.2 (2026-04-19)
-------------------

* ``demo-agent``: add Docker-based e2e test rig (``demo-agent/tests/e2e/``) that verifies Parquet output is queryable by ROSQL (GH-46)

  * Runs a turtlesim scenario (two processes under ``RMW_IMPLEMENTATION=rmw_robotops``) generating publish/subscribe, service, and action spans plus correlated log records
  * Asserts producer, consumer, and server span kinds; cross-process correlation; logs↔traces linkage; and resource attribute population via ``rosql query``
  * Topic-filter variant asserts ``ROBOTOPS_TRACE_TOPIC_FILTER`` suppresses matched topics
  * Manual-only ``workflow_dispatch`` GitHub Actions workflow (``e2e-rosql.yml``); run locally with ``just e2e``

0.9.1 (2026-04-19)
-------------------

* ``demo-agent``: align Parquet output schema with ROSQL OtelPostgres profile (GH-43)

  * ``otel_traces``: add ``timestamp`` (``TIMESTAMPTZ`` / ``Timestamp(Microsecond, UTC)``), ``service_name``, ``span_attributes`` (JSON), ``resource_attributes`` (JSON); rename ``duration_ns`` → ``duration``; ``parent_span_id`` is now a non-nullable string (empty = no parent)
  * ``otel_logs``: add ``timestamp``, ``service_name``, ``resource_attributes``, ``log_attributes`` (JSON, contains ``logger.name``, ``code.*``); rename ``severity`` → ``severity_text``
  * Subscribe spans now carry the correlated publish span as ``parent_span_id`` — enables ROSQL ``PATH DEVIATION`` and ``TRACE`` queries
  * ``service_name`` derived per-span from ROS2 node namespace + node name (e.g. ``/talker``)
  * Add ``--robot-id`` / ``--organization-id`` CLI flags (also ``ROBOTOPS_ROBOT_ID`` / ``ROBOTOPS_ORG_ID``) to populate ``resource_attributes`` for ROSQL ``WHERE robot_id = '...'`` filters
  * ROS2-specific flat columns retained as extras after the core columns (ROSQL ignores them)

0.9.0 (2026-04-03)
-------------------

* **BREAKING**: Replace DuckDB output with pure-Rust Parquet export in ``demo-agent`` — eliminates 5-10 min C++ compilation on first build
* ``demo-agent``: write OTel-compatible Parquet files to ``<output>/robotops_demo_agent/<yyyymmdd-hhmmss>/traces/`` and ``logs/`` (queryable with ``SELECT * FROM read_parquet('...')`` in DuckDB/ROSQL)
* ``demo-agent``: add S3/S3-compatible output via ``-o s3://bucket/prefix`` (reads ``AWS_PROFILE``, ``AWS_REGION``, ``AWS_ENDPOINT_URL``, ``AWS_ACCESS_KEY_ID``, ``AWS_SECRET_ACCESS_KEY``)
* ``demo-agent``: add ``--limit-mb`` flag (default 200) for graceful shutdown when storage limit is reached
* ``demo-agent``: startup greeting now prints session path, storage limit, and version
* ``demo-agent``: simplified Dockerfile — removed DuckDB system library install block
* Remove ``duckdb``, ``postgres``, and ``otlp`` feature flags from demo-agent (single portable output format)

0.8.0 (2026-04-03)
-------------------

* **NEW FEATURE**: Add ``robotops-demo-agent`` — lightweight standalone Rust demo agent for local trace evaluation (GH-38)
* ``demo-agent/`` subdirectory: standalone Cargo project, no colcon required, builds with ``cargo build --release``
* Subscribes to ``/robotops/trace_events``, ``/robotops/trace_context``, and ``/rosout`` via r2r
* Span reconstruction (START/END pairing) and cross-process correlation (GID + timestamp + content hash) ported from robot_agent
* Log-to-trace correlation via ``TraceContextRegistry`` (keyed by ROS2 logger name, matching ``/rosout`` ``Log.name`` format)
* Default output: DuckDB with OTel-compatible ``otel_traces`` and ``otel_logs`` tables conforming to ROSQL protocol (rosql.org)
* CDR-compatible message structs with round-trip unit tests for drift detection (no ``robotops-msgs`` C library dependency)
* Added "Getting started: end-to-end evaluation" section to README; not for production — use robot_agent for production deployments

0.7.0 (2026-04-03)
-------------------

* **NEW FEATURE**: Implement TraceContextChange publisher for end-to-end log-to-trace correlation (ROB-179)
* Added ``TraceContextChangePublisher``: background thread publishing to ``/robotops/trace_context`` with 512-slot MPSC ring buffer, best-effort QoS, following existing ``trace_publisher.cpp`` pattern
* Emit ``CONTEXT_ENTERED`` in ``rmw_take_with_info``, ``rmw_take_request``, ``rmw_take_response`` when thread-local trace context is set after receiving a message
* Emit ``CONTEXT_EXITED`` in ``rmw_publish`` and ``rmw_send_response`` when the downstream operation completes
* Fills the gap between ROB-94 (robot_agent subscriber) and ROB-55 (rmw_robotops trace events): ``robot_agent`` can now correlate ``/rosout`` logs with active trace spans
* Added ``get_current_thread_id()`` utility to ``utils.hpp`` for multi-threaded executor support

0.6.0 (2026-02-08)
-------------------

* **INFRASTRUCTURE**: Migrated from Cloudsmith to AWS S3-based package hosting using Aptly (ROB-127)
* Replaced Cloudsmith with AWS S3/Aptly for Debian package hosting
* Updated release.yml and release-dev.yml workflows to publish to S3 via custom GitHub action
* Removed Cloudsmith authentication requirements from Dockerfile and docker-compose.yml
* Production APT repository: https://apt.robotops.com
* Development APT repository: https://apt.development.robotops.com
* Updated CLAUDE.md documentation to reflect new package hosting infrastructure
* Cost savings: Reduced hosting costs from $149/month to ~$1-5/month
* **BREAKING**: Cloudsmith credentials (CLOUDSMITH_USERNAME, CLOUDSMITH_API_KEY) no longer used
* **BREAKING**: Optional APT_REPO_URL environment variable replaces CLOUDSMITH_REPO
* No authentication required - all packages are now publicly accessible

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
