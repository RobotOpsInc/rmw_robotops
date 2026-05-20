//! robotops-demo-agent — lightweight demo agent for evaluating rmw_robotops observability.
//!
//! Subscribes to:
//!   - `/robotops/trace_events`   → span reconstruction + cross-process correlation
//!   - `/robotops/trace_context`  → log-to-trace correlation context
//!   - `/rosout`                  → log capture with trace enrichment
//!
//! Writes OTel-compatible Parquet files conforming to the ROSQL schema (see rosql.org).
//!
//! **This is a demo/evaluation tool — not for production.** For production deployments
//! with system metrics, TF monitoring, MCAP recording, offline buffering, and fleet
//! management, use TraceHouse's robot_agent: https://robotops.com.

mod cli;
mod error;
mod export;
mod messages;
mod pipeline;
mod ros2;
mod subscribers;

use crate::cli::Cli;
use crate::error::BridgeError;
use crate::export::parquet::ParquetExporter;
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

const VERSION: &str = env!("CARGO_PKG_VERSION");

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let cli = Cli::parse();

    // Initialise structured logging (RUST_LOG controls level; default: info)
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    // Build resource_attributes JSON from optional CLI flags.
    let resource_attrs_json = {
        let mut attrs = serde_json::Map::new();
        if !cli.robot_id.is_empty() {
            attrs.insert("robot.id".into(), cli.robot_id.clone().into());
        }
        if !cli.organization_id.is_empty() {
            attrs.insert("organization.id".into(), cli.organization_id.clone().into());
        }
        if attrs.is_empty() {
            "{}".to_string()
        } else {
            serde_json::to_string(&attrs).unwrap_or_else(|_| "{}".to_string())
        }
    };

    // Create exporter — get session path before boxing so we can show it in the greeting
    let parquet = ParquetExporter::new(
        &cli.output,
        cli.batch_size,
        cli.limit_mb,
        resource_attrs_json,
    )
    .map_err(|e| anyhow::anyhow!("Failed to initialise output: {}", e))?;
    let session_display = format!(
        "{}/{}",
        cli.output.trim_end_matches('/'),
        parquet.session_path()
    );
    let mut exporter: Box<dyn export::SpanExporter> = Box::new(parquet);

    // Startup greeting — always printed to stdout regardless of log level
    println!();
    println!("robotops-demo-agent v{VERSION}");
    println!("Writing to:    {session_display}");
    println!("Storage limit: {} MB", cli.limit_mb);
    println!();
    println!("Subscribing to /robotops/trace_events, /robotops/trace_context, /rosout...");
    println!("Press Ctrl-C to stop.");
    println!();
    println!("For production-grade telemetry with fleet management, MCAP recording,");
    println!("and offline buffering, visit https://robotops.com");
    println!("Questions? hello@robotops.com");
    println!();

    // Shared pipeline components
    let registry = Arc::new(TraceContextRegistry::new());
    let reconstructor = Arc::new(Mutex::new(SpanReconstructor::new()));
    let correlation_config = CorrelationConfig {
        timestamp_tolerance_ns: cli.correlation_tolerance_ns,
        window_secs: cli.correlation_window_secs,
        hash_enabled: true,
    };
    let correlation = Arc::new(Mutex::new(CorrelationEngine::new(correlation_config)));

    // Export channel — subscribers → main loop
    let (export_tx, mut export_rx) = mpsc::unbounded_channel::<ExportRecord>();

    // ROS2 context (creates shared node + spin thread)
    let ros2 =
        Ros2Context::new().map_err(|e| anyhow::anyhow!("Failed to initialise ROS2: {}", e))?;
    let node = ros2.node();

    info!("ROS2 node created, subscribing to topics...");

    subscribers::trace_subscriber::spawn(
        node.clone(),
        reconstructor.clone(),
        correlation.clone(),
        export_tx.clone(),
    )?;

    subscribers::context_subscriber::spawn(node.clone(), registry.clone())?;

    subscribers::rosout_subscriber::spawn(node.clone(), registry.clone(), export_tx.clone())?;

    info!("All subscribers active");

    // Periodic cleanup of the correlation window
    let correlation_cleanup = correlation.clone();
    tokio::spawn(async move {
        let mut interval = time::interval(Duration::from_secs(10));
        loop {
            interval.tick().await;
            correlation_cleanup.lock().cleanup_expired();
        }
    });

    let batch_size = cli.batch_size;
    let flush_interval = Duration::from_millis(cli.flush_interval_ms);

    let mut flush_timer = time::interval(flush_interval);
    flush_timer.set_missed_tick_behavior(time::MissedTickBehavior::Skip);
    flush_timer.tick().await; // Don't fire immediately

    let mut limit_reached = false;

    loop {
        tokio::select! {
            _ = signal::ctrl_c() => {
                info!("Shutdown signal received, flushing...");
                break;
            }

            _ = flush_timer.tick() => {
                drain_channel(&mut export_rx, &mut *exporter, batch_size);
                match exporter.flush() {
                    Ok(()) => {}
                    Err(BridgeError::StorageLimitReached) => {
                        println!(
                            "\nStorage limit of {} MB reached — flushing and shutting down.",
                            cli.limit_mb
                        );
                        limit_reached = true;
                        break;
                    }
                    Err(e) => error!("Flush error: {}", e),
                }
            }

            Some(record) = export_rx.recv() => {
                match write_record(&mut *exporter, record) {
                    Ok(()) => {}
                    Err(BridgeError::StorageLimitReached) => {
                        println!(
                            "\nStorage limit of {} MB reached — flushing and shutting down.",
                            cli.limit_mb
                        );
                        limit_reached = true;
                        break;
                    }
                    Err(e) => tracing::warn!("Export error: {}", e),
                }
                drain_channel(&mut export_rx, &mut *exporter, batch_size.saturating_sub(1));
            }
        }
    }

    // Final flush — best-effort, storage limit on shutdown is acceptable
    drop(export_tx);
    while let Ok(record) = export_rx.try_recv() {
        let _ = write_record(&mut *exporter, record);
    }
    match exporter.flush() {
        Ok(()) | Err(BridgeError::StorageLimitReached) => {}
        Err(e) => error!("Final flush error: {}", e),
    }

    let (pub_pending, take_pending, action_pending, svc_pending) =
        reconstructor.lock().pending_counts();
    if pub_pending + take_pending + action_pending + svc_pending > 0 {
        tracing::warn!(
            publish = pub_pending,
            subscribe = take_pending,
            actions = action_pending,
            services = svc_pending,
            "Shutdown with unmatched START events (some spans will be incomplete)"
        );
    }

    if limit_reached {
        println!("Data written to: {session_display}");
    }
    info!("robotops-demo-agent stopped cleanly");
    Ok(())
}

fn drain_channel(
    rx: &mut mpsc::UnboundedReceiver<ExportRecord>,
    exporter: &mut dyn export::SpanExporter,
    max: usize,
) {
    for _ in 0..max {
        match rx.try_recv() {
            Ok(record) => {
                let _ = write_record(exporter, record);
            }
            Err(_) => break,
        }
    }
}

fn write_record(
    exporter: &mut dyn export::SpanExporter,
    record: ExportRecord,
) -> crate::error::Result<()> {
    match record {
        ExportRecord::Span(span) => exporter.export_span(&span),
        ExportRecord::Log(log) => exporter.export_log(&log),
    }
}
