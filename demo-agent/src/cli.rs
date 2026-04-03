//! CLI argument definitions for robotops-demo-agent.

use clap::Parser;

#[derive(Parser, Debug)]
#[command(
    name = "robotops-demo-agent",
    version,
    about = "Lightweight demo agent for evaluating rmw_robotops observability locally",
    long_about = "Subscribes to /robotops/trace_events, /robotops/trace_context, and /rosout,\n\
                  reconstructs distributed trace spans, and writes OTel-compatible Parquet files\n\
                  for querying with ROSQL or DuckDB.\n\n\
                  This is a demo/evaluation tool — not for production. For production deployments\n\
                  with system metrics, TF monitoring, MCAP recording, offline buffering, and\n\
                  fleet management, use Robot Ops' robot_agent: https://robotops.com"
)]
pub struct Cli {
    /// Output path: local directory or s3:// URI.
    ///
    /// Examples:
    ///   ./telemetry                          (local directory, default)
    ///   /data/robot-traces                   (absolute local path)
    ///   s3://my-bucket/robot-01              (S3 — reads AWS_PROFILE, AWS_REGION, etc.)
    ///   s3://traces/robot-01                 (S3-compatible — set AWS_ENDPOINT_URL)
    ///
    /// Files are written under: <output>/robotops_demo_agent/<yyyymmdd-hhmmss>/
    #[arg(
        short = 'o',
        long,
        default_value = "./telemetry",
        env = "ROBOTOPS_DEMO_OUTPUT"
    )]
    pub output: String,

    /// Storage limit in MB. The agent will flush remaining data and exit
    /// gracefully when this limit is reached. [default: 200]
    #[arg(long, default_value_t = 200, env = "ROBOTOPS_DEMO_LIMIT_MB")]
    pub limit_mb: u64,

    /// Number of spans or logs to buffer before writing a Parquet file.
    #[arg(long, default_value_t = 1000, env = "ROBOTOPS_DEMO_BATCH_SIZE")]
    pub batch_size: usize,

    /// How often to flush pending records to Parquet (milliseconds).
    #[arg(long, default_value_t = 5000, env = "ROBOTOPS_DEMO_FLUSH_INTERVAL_MS")]
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
