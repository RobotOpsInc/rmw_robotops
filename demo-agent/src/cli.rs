//! CLI argument definitions for robotops-demo-agent.

use clap::Parser;

#[derive(Parser, Debug)]
#[command(
    name = "robotops-demo-agent",
    about = "Lightweight demo agent for evaluating rmw_robotops observability locally",
    long_about = "Subscribes to /robotops/trace_events, /robotops/trace_context, and /rosout,\n\
                  reconstructs distributed trace spans, and exports OTel-compatible telemetry\n\
                  to a local data store for querying with ROSQL.\n\n\
                  This is a demo/evaluation tool — not for production. For production deployments\n\
                  with system metrics, TF monitoring, MCAP recording, offline buffering, and\n\
                  fleet management, use Robot Ops' robot_agent: https://robotops.com"
)]
pub struct Cli {
    /// Output destination URI.
    ///
    /// Supported formats:
    ///   duckdb:///path/to/telemetry.db   (default, no extra features needed)
    ///   postgres://user:pass@host/db      (requires --features postgres)
    ///   otlp://host:4317                  (requires --features otlp)
    #[arg(
        long,
        default_value = "duckdb:///telemetry.db",
        env = "ROBOTOPS_DEMO_OUTPUT"
    )]
    pub output: String,

    /// Number of records to accumulate before flushing to the database.
    #[arg(long, default_value_t = 100, env = "ROBOTOPS_DEMO_BATCH_SIZE")]
    pub batch_size: usize,

    /// How often to flush pending records to the database (milliseconds).
    #[arg(long, default_value_t = 1000, env = "ROBOTOPS_DEMO_FLUSH_INTERVAL_MS")]
    pub flush_interval_ms: u64,

    /// Correlation window: how long to keep unmatched publish events (seconds).
    #[arg(
        long,
        default_value_t = 30,
        env = "ROBOTOPS_DEMO_CORRELATION_WINDOW_SECS"
    )]
    pub correlation_window_secs: u32,

    /// Timestamp tolerance for cross-process correlation (nanoseconds).
    /// Publish and subscribe events within this window are eligible for matching.
    #[arg(
        long,
        default_value_t = 10_000_000,
        env = "ROBOTOPS_DEMO_CORRELATION_TOLERANCE_NS"
    )]
    pub correlation_tolerance_ns: u64,

    /// ROS2 domain ID. Defaults to ROS_DOMAIN_ID environment variable, or 0.
    #[arg(long, env = "ROS_DOMAIN_ID")]
    pub domain_id: Option<u32>,
}
