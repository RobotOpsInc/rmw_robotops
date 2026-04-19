//! Subscriber for `/robotops/trace_events` (TraceEvent messages).
//!
//! Feeds events through `SpanReconstructor` → `CorrelationEngine` →
//! export channel. Publish spans are recorded in the correlation engine;
//! subscribe spans are correlated against previously recorded publish spans.
//!
//! Adapted from robot_agent's distributed tracing subscriber, simplified by
//! removing tail-based sampling (the bridge exports all completed spans).

use crate::messages::TraceEvent;
use crate::pipeline::correlation_engine::CorrelationEngine;
use crate::pipeline::otel_builder::{ExportRecord, OtelSpan};
use crate::pipeline::span_reconstructor::SpanReconstructor;
use futures::StreamExt;
use parking_lot::Mutex;
use std::sync::Arc;
use tokio::sync::mpsc::UnboundedSender;
use tracing::{debug, warn};

const TRACE_EVENTS_TOPIC: &str = "/robotops/trace_events";
const TRACE_EVENTS_MSG_TYPE: &str = "robotops_msgs/msg/TraceEvent";

/// Spawn a tokio task that subscribes to `/robotops/trace_events`, reconstructs
/// spans, correlates cross-process pub/sub pairs, and sends `OtelSpan` records
/// to `export_tx`.
///
/// `reconstructor` and `correlation` are shared with the periodic cleanup task
/// in `main.rs` (they hold the `parking_lot::Mutex`).
pub fn spawn(
    node: Arc<Mutex<r2r::Node>>,
    reconstructor: Arc<parking_lot::Mutex<SpanReconstructor>>,
    correlation: Arc<parking_lot::Mutex<CorrelationEngine>>,
    export_tx: UnboundedSender<ExportRecord>,
) -> anyhow::Result<()> {
    let subscription = {
        let mut guard = node.lock();
        guard
            .subscribe_raw(
                TRACE_EVENTS_TOPIC,
                TRACE_EVENTS_MSG_TYPE,
                // trace publisher uses BEST_EFFORT; subscriber must match or DDS won't deliver
                r2r::QosProfile::default().best_effort(),
            )
            .map_err(|e| anyhow::anyhow!("Failed to subscribe to {}: {}", TRACE_EVENTS_TOPIC, e))?
    };

    debug!("Subscribed to {}", TRACE_EVENTS_TOPIC);

    tokio::spawn(async move {
        subscription
            .for_each(|bytes| {
                match cdr::deserialize::<TraceEvent>(&bytes) {
                    Ok(event) => process_event(event, &reconstructor, &correlation, &export_tx),
                    Err(e) => warn!("Failed to deserialize TraceEvent: {}", e),
                }
                futures::future::ready(())
            })
            .await;
        debug!("{} subscription stream ended", TRACE_EVENTS_TOPIC);
    });

    Ok(())
}

fn process_event(
    event: TraceEvent,
    reconstructor: &Arc<parking_lot::Mutex<SpanReconstructor>>,
    correlation: &Arc<parking_lot::Mutex<CorrelationEngine>>,
    export_tx: &UnboundedSender<ExportRecord>,
) {
    let event_type = event.event_type;
    let source_ts = event.source_timestamp_ns;
    let content_hash = event.content_hash;
    let publisher_gid = event.publisher_gid.clone();

    // Try to complete a START/END pair
    let maybe_span = reconstructor.lock().process_event(event);

    let span = match maybe_span {
        Some(s) => s,
        None => return, // START buffered or unmatched END
    };

    // Cross-process correlation — use span.topic_or_service now that we own it
    let correlated_publish_span_id = match event_type {
        TraceEvent::EVENT_PUBLISH_RMW_END => {
            // Record this publish span so future subscribe events can find it
            correlation.lock().record_publish(
                span.topic_or_service.clone(),
                source_ts,
                content_hash,
                publisher_gid,
                span.span_id.clone(),
            );
            None
        }
        TraceEvent::EVENT_TAKE_RMW_END => {
            // Try to match against a recorded publish
            correlation.lock().correlate_subscribe(
                &span.topic_or_service,
                source_ts,
                content_hash,
                &publisher_gid,
            )
        }
        _ => None,
    };

    let otel_span = OtelSpan::from_span(span, correlated_publish_span_id);

    if export_tx.send(ExportRecord::Span(otel_span)).is_err() {
        debug!("Export channel closed, stopping trace subscriber");
    }
}
