//! Subscriber for `/robotops/trace_context` (TraceContextChange messages).
//!
//! Deserializes CDR bytes and updates the shared `TraceContextRegistry` so
//! that `/rosout` messages can be correlated with their active trace context.
//!
//! Adapted from robot_agent's `src/logging/trace_context.rs`.

use crate::messages::TraceContextChange;
use crate::pipeline::trace_context::{handle_context_change, TraceContextRegistry};
use futures::StreamExt;
use parking_lot::Mutex;
use std::sync::Arc;
use tracing::{debug, warn};

const TRACE_CONTEXT_TOPIC: &str = "/robotops/trace_context";
const TRACE_CONTEXT_MSG_TYPE: &str = "robotops_msgs/msg/TraceContextChange";

/// Spawn a tokio task that subscribes to `/robotops/trace_context` and keeps
/// `registry` up to date. The task runs until the subscription stream closes.
pub fn spawn(
    node: Arc<Mutex<r2r::Node>>,
    registry: Arc<TraceContextRegistry>,
) -> anyhow::Result<()> {
    let subscription = {
        let mut guard = node.lock();
        guard
            .subscribe_raw(
                TRACE_CONTEXT_TOPIC,
                TRACE_CONTEXT_MSG_TYPE,
                r2r::QosProfile::default(),
            )
            .map_err(|e| anyhow::anyhow!("Failed to subscribe to {}: {}", TRACE_CONTEXT_TOPIC, e))?
    };

    debug!("Subscribed to {}", TRACE_CONTEXT_TOPIC);

    tokio::spawn(async move {
        subscription
            .for_each(|bytes| {
                match cdr::deserialize::<TraceContextChange>(&bytes) {
                    Ok(event) => handle_context_change(&event, &registry),
                    Err(e) => warn!("Failed to deserialize TraceContextChange: {}", e),
                }
                futures::future::ready(())
            })
            .await;
        debug!("{} subscription stream ended", TRACE_CONTEXT_TOPIC);
    });

    Ok(())
}
