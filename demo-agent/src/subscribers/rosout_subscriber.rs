//! Subscriber for `/rosout` (rcl_interfaces/msg/Log messages).
//!
//! Deserializes CDR bytes, optionally enriches with trace context from the
//! registry, and sends `OtelLog` records to the export channel.
//!
//! Adapted from robot_agent's `src/logging/rosout_subscriber.rs`.

use crate::messages::RosoutLog;
use crate::pipeline::otel_builder::{ExportRecord, OtelLog};
use crate::pipeline::trace_context::TraceContextRegistry;
use futures::StreamExt;
use parking_lot::Mutex;
use std::sync::Arc;
use tokio::sync::mpsc::UnboundedSender;
use tracing::{debug, warn};

const ROSOUT_TOPIC: &str = "/rosout";
const ROSOUT_MSG_TYPE: &str = "rcl_interfaces/msg/Log";

/// Spawn a tokio task that subscribes to `/rosout` and forwards `OtelLog`
/// records (with optional trace context) to `export_tx`.
pub fn spawn(
    node: Arc<Mutex<r2r::Node>>,
    registry: Arc<TraceContextRegistry>,
    export_tx: UnboundedSender<ExportRecord>,
) -> anyhow::Result<()> {
    let subscription = {
        let mut guard = node.lock();
        guard
            .subscribe_raw(ROSOUT_TOPIC, ROSOUT_MSG_TYPE, r2r::QosProfile::default())
            .map_err(|e| anyhow::anyhow!("Failed to subscribe to {}: {}", ROSOUT_TOPIC, e))?
    };

    debug!("Subscribed to {}", ROSOUT_TOPIC);

    tokio::spawn(async move {
        subscription
            .for_each(|bytes| {
                match cdr::deserialize::<RosoutLog>(&bytes) {
                    Ok(log) => {
                        let timestamp_ns = log.stamp.to_ns();
                        let (severity, severity_number) = log.severity();

                        // Look up trace context keyed by ROS2 logger name
                        let (trace_id, span_id) = registry
                            .get_context(&log.name)
                            .map(|ctx| (Some(ctx.trace_id), Some(ctx.span_id)))
                            .unwrap_or((None, None));

                        let record = OtelLog {
                            timestamp_ns,
                            severity: severity.to_string(),
                            severity_number,
                            body: log.msg,
                            logger_name: log.name,
                            trace_id,
                            span_id,
                            code_filepath: if log.file.is_empty() {
                                None
                            } else {
                                Some(log.file)
                            },
                            code_function: if log.function.is_empty() {
                                None
                            } else {
                                Some(log.function)
                            },
                            code_lineno: if log.line == 0 { None } else { Some(log.line) },
                        };

                        if export_tx.send(ExportRecord::Log(record)).is_err() {
                            debug!("Export channel closed, stopping rosout subscriber");
                        }
                    }
                    Err(e) => warn!("Failed to deserialize RosoutLog: {}", e),
                }
                futures::future::ready(())
            })
            .await;
        debug!("{} subscription stream ended", ROSOUT_TOPIC);
    });

    Ok(())
}
