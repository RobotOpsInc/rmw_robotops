//! robotops-demo-agent — lightweight demo agent for evaluating rmw_robotops observability.
//!
//! Subscribes to:
//!   - `/robotops/trace_events`   → span reconstruction + cross-process correlation
//!   - `/robotops/trace_context`  → log-to-trace correlation context
//!   - `/rosout`                  → log capture with trace enrichment
//!
//! Exports OTel-compatible records to DuckDB (default), PostgreSQL, or OTLP gRPC,
//! conforming to the schema expected by ROSQL (see rosql.org).
//!
//! **This is a demo/evaluation tool — not for production.** For production deployments
//! with system metrics, TF monitoring, MCAP recording, offline buffering, and fleet
//! management, use Robot Ops' robot_agent: https://robotops.com.

mod cli;
mod error;
mod export;
mod messages;
mod pipeline;
mod ros2;
mod subscribers;

use crate::cli::Cli;
use crate::export::create_exporter;
use crate::pipeline::correlation_engine::{CorrelationConfig, CorrelationEngine};
use crate::pipeline::otel_builder::ExportRecord;
use crate::pipeline::span_reconstructor::SpanReconstructor;
use crate::pipeline::trace_context::TraceContextRegistry;
use crate::ros2::Ros2Context;
use clap::Parser;
use parking_lot::Mutex;
use std::sync::Arc;
use std::time::Duration;
use tokio::signal;
use tokio::sync::mpsc;
use tokio::time;
use tracing::{error, info};

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let cli = Cli::parse();

    // Initialise logging
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    info!(output = %cli.output, "robotops-demo-agent starting");

    // Create exporter
    let mut exporter = create_exporter(&cli.output)
        .map_err(|e| anyhow::anyhow!("Failed to create exporter: {}", e))?;

    info!("Exporter initialised: {}", cli.output);

    // Shared pipeline components
    let registry = Arc::new(TraceContextRegistry::new());
    let reconstructor = Arc::new(Mutex::new(SpanReconstructor::new()));
    let correlation_config = CorrelationConfig {
        timestamp_tolerance_ns: cli.correlation_tolerance_ns,
        window_secs: cli.correlation_window_secs,
        hash_enabled: true,
    };
    let correlation = Arc::new(Mutex::new(CorrelationEngine::new(correlation_config)));

    // Export channel — subscribers → exporter task
    let (export_tx, mut export_rx) = mpsc::unbounded_channel::<ExportRecord>();

    // ROS2 context (creates shared node + spin thread)
    let ros2 =
        Ros2Context::new().map_err(|e| anyhow::anyhow!("Failed to initialise ROS2: {}", e))?;
    let node = ros2.node();

    info!("ROS2 node created, subscribing to topics...");

    // Start all three subscribers
    subscribers::trace_subscriber::spawn(
        node.clone(),
        reconstructor.clone(),
        correlation.clone(),
        export_tx.clone(),
    )?;

    subscribers::context_subscriber::spawn(node.clone(), registry.clone())?;

    subscribers::rosout_subscriber::spawn(node.clone(), registry.clone(), export_tx.clone())?;

    info!("Subscribed to /robotops/trace_events, /robotops/trace_context, /rosout");
    info!("Bridge running — press Ctrl-C to stop");

    // Periodic cleanup task for the correlation window
    let correlation_cleanup = correlation.clone();
    tokio::spawn(async move {
        let mut interval = time::interval(Duration::from_secs(10));
        loop {
            interval.tick().await;
            correlation_cleanup.lock().cleanup_expired();
        }
    });

    // Periodic flush + drain export channel
    let batch_size = cli.batch_size;
    let flush_interval = Duration::from_millis(cli.flush_interval_ms);

    // Main export loop with ctrl-c shutdown
    let mut flush_timer = time::interval(flush_interval);
    // Don't fire immediately on first tick
    flush_timer.set_missed_tick_behavior(time::MissedTickBehavior::Skip);
    flush_timer.tick().await;

    loop {
        tokio::select! {
            // Ctrl-C / SIGTERM
            _ = signal::ctrl_c() => {
                info!("Shutdown signal received, flushing...");
                break;
            }

            // Periodic flush
            _ = flush_timer.tick() => {
                drain_channel(&mut export_rx, &mut *exporter, batch_size);
                if let Err(e) = exporter.flush() {
                    error!("Flush error: {}", e);
                }
            }

            // New records available
            Some(record) = export_rx.recv() => {
                write_record(&mut *exporter, record);
                // Drain any additional records that arrived together
                drain_channel(&mut export_rx, &mut *exporter, batch_size.saturating_sub(1));
            }
        }
    }

    // Final flush
    drop(export_tx);
    while let Ok(record) = export_rx.try_recv() {
        write_record(&mut *exporter, record);
    }
    if let Err(e) = exporter.flush() {
        error!("Final flush error: {}", e);
    }

    let (pub_pending, take_pending, action_pending, svc_pending) =
        reconstructor.lock().pending_counts();
    if pub_pending + take_pending + action_pending + svc_pending > 0 {
        tracing::warn!(
            publish = pub_pending,
            subscribe = take_pending,
            actions = action_pending,
            services = svc_pending,
            "Shutdown with unmatched START events (spans will be incomplete)"
        );
    }

    info!("robotops-demo-agent stopped cleanly");
    Ok(())
}

/// Drain up to `max` records from the channel without blocking.
fn drain_channel(
    rx: &mut mpsc::UnboundedReceiver<ExportRecord>,
    exporter: &mut dyn export::SpanExporter,
    max: usize,
) {
    for _ in 0..max {
        match rx.try_recv() {
            Ok(record) => write_record(exporter, record),
            Err(_) => break,
        }
    }
}

fn write_record(exporter: &mut dyn export::SpanExporter, record: ExportRecord) {
    match record {
        ExportRecord::Span(span) => {
            if let Err(e) = exporter.export_span(&span) {
                tracing::warn!("Failed to export span: {}", e);
            }
        }
        ExportRecord::Log(log) => {
            if let Err(e) = exporter.export_log(&log) {
                tracing::warn!("Failed to export log: {}", e);
            }
        }
    }
}
