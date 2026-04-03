//! Span reconstruction from START/END event pairs.
//!
//! Buffers START events until their corresponding END events arrive, then emits
//! complete duration spans. Ported from robot_agent's
//! `src/distributed_tracing/span_reconstructor.rs` with an expanded
//! `ReconstructedSpan` to carry all OTel-relevant fields.
//!
//! # Event pairing
//!
//! | START event            | END event              | operation          |
//! |------------------------|------------------------|--------------------|
//! | EVENT_PUBLISH_RMW_START | EVENT_PUBLISH_RMW_END | "publish"          |
//! | EVENT_TAKE_RMW_START   | EVENT_TAKE_RMW_END     | "subscribe"        |
//! | EVENT_SERVICE_REQUEST  | EVENT_SERVICE_RESPONSE | "service"          |
//! | EVENT_ACTION_GOAL      | EVENT_ACTION_RESULT    | "action"           |
//! | EVENT_ACTION_GOAL      | EVENT_ACTION_CANCEL    | "action_cancelled" |

use crate::messages::TraceEvent;
use std::collections::HashMap;
use tracing::{debug, warn};

/// A fully reconstructed span with start/end timing and OTel attributes.
#[derive(Debug, Clone)]
pub struct ReconstructedSpan {
    pub trace_id: String,
    pub span_id: String,
    pub parent_span_id: String,
    pub span_links: Vec<String>,
    pub operation: String,
    pub topic_or_service: String,
    pub node_name: String,
    pub node_namespace: String,
    pub message_type: String,
    pub publisher_gid: String,
    pub content_hash: u64,
    pub sequence_number: u64,
    pub source_timestamp_ns: i64,
    pub message_size_bytes: u32,
    pub dds_domain_id: u32,
    pub correlation_method: u8,
    pub start_time_ns: i64,
    pub end_time_ns: i64,
    pub duration_ns: i64,
}

impl ReconstructedSpan {
    fn from_events(start: &TraceEvent, end: &TraceEvent, operation: &str) -> Self {
        let start_ns = start.timestamp.to_ns();
        let end_ns = end.timestamp.to_ns();
        Self {
            trace_id: start.trace_id.clone(),
            span_id: start.span_id.clone(),
            parent_span_id: start.parent_span_id.clone(),
            span_links: start.span_links.clone(),
            operation: operation.to_string(),
            topic_or_service: start.topic_or_service.clone(),
            node_name: start.node_name.clone(),
            node_namespace: start.node_namespace.clone(),
            message_type: start.message_type.clone(),
            publisher_gid: start.publisher_gid.clone(),
            content_hash: start.content_hash,
            sequence_number: start.sequence_number,
            source_timestamp_ns: start.source_timestamp_ns,
            message_size_bytes: start.message_size_bytes,
            dds_domain_id: start.dds_domain_id,
            correlation_method: start.correlation_method,
            start_time_ns: start_ns,
            end_time_ns: end_ns,
            duration_ns: end_ns - start_ns,
        }
    }
}

/// Buffers START events and emits complete spans when their END events arrive.
#[derive(Default)]
pub struct SpanReconstructor {
    pending_publish_starts: HashMap<String, TraceEvent>,
    pending_take_starts: HashMap<String, TraceEvent>,
    pending_action_goals: HashMap<String, TraceEvent>,
    pending_service_requests: HashMap<String, TraceEvent>,
}

impl SpanReconstructor {
    pub fn new() -> Self {
        Self::default()
    }

    /// Process a single trace event. Returns a completed span if this event
    /// closes a pending START, otherwise returns `None`.
    pub fn process_event(&mut self, event: TraceEvent) -> Option<ReconstructedSpan> {
        match event.event_type {
            TraceEvent::EVENT_PUBLISH_RMW_START => {
                debug!(span_id = %event.span_id, topic = %event.topic_or_service, "Buffering publish START");
                self.pending_publish_starts
                    .insert(event.span_id.clone(), event);
                None
            }
            TraceEvent::EVENT_PUBLISH_RMW_END => {
                if let Some(start) = self.pending_publish_starts.remove(&event.span_id) {
                    debug!(span_id = %event.span_id, "Matched publish START/END");
                    Some(ReconstructedSpan::from_events(&start, &event, "publish"))
                } else {
                    warn!(span_id = %event.span_id, "Publish END without matching START");
                    None
                }
            }

            TraceEvent::EVENT_TAKE_RMW_START => {
                debug!(span_id = %event.span_id, topic = %event.topic_or_service, "Buffering take START");
                self.pending_take_starts
                    .insert(event.span_id.clone(), event);
                None
            }
            TraceEvent::EVENT_TAKE_RMW_END => {
                if let Some(start) = self.pending_take_starts.remove(&event.span_id) {
                    debug!(span_id = %event.span_id, "Matched take START/END");
                    Some(ReconstructedSpan::from_events(&start, &event, "subscribe"))
                } else {
                    warn!(span_id = %event.span_id, "Take END without matching START");
                    None
                }
            }

            TraceEvent::EVENT_SERVICE_REQUEST => {
                debug!(span_id = %event.span_id, service = %event.topic_or_service, "Buffering service REQUEST");
                self.pending_service_requests
                    .insert(event.span_id.clone(), event);
                None
            }
            TraceEvent::EVENT_SERVICE_RESPONSE => {
                if let Some(req) = self.pending_service_requests.remove(&event.span_id) {
                    debug!(span_id = %event.span_id, "Matched service REQUEST/RESPONSE");
                    Some(ReconstructedSpan::from_events(&req, &event, "service"))
                } else {
                    warn!(span_id = %event.span_id, "Service RESPONSE without matching REQUEST");
                    None
                }
            }

            TraceEvent::EVENT_ACTION_GOAL => {
                debug!(span_id = %event.span_id, action = %event.topic_or_service, "Buffering action GOAL");
                self.pending_action_goals
                    .insert(event.span_id.clone(), event);
                None
            }
            TraceEvent::EVENT_ACTION_RESULT => {
                if let Some(goal) = self.pending_action_goals.remove(&event.span_id) {
                    debug!(span_id = %event.span_id, "Matched action GOAL/RESULT");
                    Some(ReconstructedSpan::from_events(&goal, &event, "action"))
                } else {
                    warn!(span_id = %event.span_id, "Action RESULT without matching GOAL");
                    None
                }
            }
            TraceEvent::EVENT_ACTION_CANCEL => {
                if let Some(goal) = self.pending_action_goals.remove(&event.span_id) {
                    debug!(span_id = %event.span_id, "Matched action GOAL/CANCEL");
                    Some(ReconstructedSpan::from_events(
                        &goal,
                        &event,
                        "action_cancelled",
                    ))
                } else {
                    warn!(span_id = %event.span_id, "Action CANCEL without matching GOAL");
                    None
                }
            }

            TraceEvent::EVENT_ACTION_FEEDBACK => {
                debug!(span_id = %event.span_id, "Ignoring action FEEDBACK (no duration span)");
                None
            }

            _ => {
                warn!(event_type = event.event_type, span_id = %event.span_id, "Unknown event type");
                None
            }
        }
    }

    /// Pending event counts, useful for diagnostics.
    pub fn pending_counts(&self) -> (usize, usize, usize, usize) {
        (
            self.pending_publish_starts.len(),
            self.pending_take_starts.len(),
            self.pending_action_goals.len(),
            self.pending_service_requests.len(),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::messages::CdrTime;

    fn make_event(event_type: u8, span_id: &str, timestamp_sec: i32) -> TraceEvent {
        TraceEvent {
            timestamp: CdrTime {
                sec: timestamp_sec,
                nanosec: 0,
            },
            event_type,
            trace_id: "trace-1".into(),
            span_id: span_id.into(),
            topic_or_service: "/test".into(),
            node_name: "node".into(),
            node_namespace: "/".into(),
            message_type: "std_msgs/msg/String".into(),
            ..Default::default()
        }
    }

    #[test]
    fn test_publish_start_end() {
        let mut r = SpanReconstructor::new();
        let start = make_event(TraceEvent::EVENT_PUBLISH_RMW_START, "s1", 0);
        let end = make_event(TraceEvent::EVENT_PUBLISH_RMW_END, "s1", 1);
        assert!(r.process_event(start).is_none());
        let span = r.process_event(end).unwrap();
        assert_eq!(span.operation, "publish");
        assert_eq!(span.duration_ns, 1_000_000_000);
    }

    #[test]
    fn test_take_start_end() {
        let mut r = SpanReconstructor::new();
        let start = make_event(TraceEvent::EVENT_TAKE_RMW_START, "s2", 0);
        let end = make_event(TraceEvent::EVENT_TAKE_RMW_END, "s2", 2);
        assert!(r.process_event(start).is_none());
        let span = r.process_event(end).unwrap();
        assert_eq!(span.operation, "subscribe");
        assert_eq!(span.duration_ns, 2_000_000_000);
    }

    #[test]
    fn test_service_request_response() {
        let mut r = SpanReconstructor::new();
        let req = make_event(TraceEvent::EVENT_SERVICE_REQUEST, "s3", 0);
        let resp = make_event(TraceEvent::EVENT_SERVICE_RESPONSE, "s3", 5);
        assert!(r.process_event(req).is_none());
        let span = r.process_event(resp).unwrap();
        assert_eq!(span.operation, "service");
    }

    #[test]
    fn test_action_goal_result() {
        let mut r = SpanReconstructor::new();
        let goal = make_event(TraceEvent::EVENT_ACTION_GOAL, "s4", 0);
        let result = make_event(TraceEvent::EVENT_ACTION_RESULT, "s4", 10);
        assert!(r.process_event(goal).is_none());
        let span = r.process_event(result).unwrap();
        assert_eq!(span.operation, "action");
    }

    #[test]
    fn test_action_goal_cancel() {
        let mut r = SpanReconstructor::new();
        let goal = make_event(TraceEvent::EVENT_ACTION_GOAL, "s5", 0);
        let cancel = make_event(TraceEvent::EVENT_ACTION_CANCEL, "s5", 3);
        assert!(r.process_event(goal).is_none());
        let span = r.process_event(cancel).unwrap();
        assert_eq!(span.operation, "action_cancelled");
    }

    #[test]
    fn test_end_without_start_returns_none() {
        let mut r = SpanReconstructor::new();
        let end = make_event(TraceEvent::EVENT_PUBLISH_RMW_END, "missing", 1);
        assert!(r.process_event(end).is_none());
    }

    #[test]
    fn test_feedback_returns_none() {
        let mut r = SpanReconstructor::new();
        let fb = make_event(TraceEvent::EVENT_ACTION_FEEDBACK, "s6", 0);
        assert!(r.process_event(fb).is_none());
    }
}
