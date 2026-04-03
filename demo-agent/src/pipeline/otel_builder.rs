//! OTel-compatible record builder.
//!
//! Converts `ReconstructedSpan` and correlated log messages into `OtelSpan`
//! and `OtelLog` records ready for database export.

use crate::pipeline::span_reconstructor::ReconstructedSpan;

// ---------------------------------------------------------------------------
// OTel span
// ---------------------------------------------------------------------------

/// OTel-compatible span record for database export.
#[derive(Debug, Clone)]
pub struct OtelSpan {
    pub trace_id: String,
    pub span_id: String,
    pub parent_span_id: Option<String>,
    pub operation: String,
    pub span_name: String,
    pub span_kind: SpanKind,
    pub start_time_ns: i64,
    pub end_time_ns: i64,
    pub duration_ns: i64,
    pub status_code: String,
    // ROS2 attributes
    pub ros_topic: Option<String>,
    pub ros_node_name: String,
    pub ros_node_namespace: String,
    pub ros_message_type: String,
    pub ros_publisher_gid: String,
    pub ros_content_hash: u64,
    pub ros_sequence_number: u64,
    pub ros_source_timestamp_ns: i64,
    pub ros_message_size_bytes: u32,
    pub ros_dds_domain_id: u32,
    pub ros_correlation_method: u8,
    // Cross-process correlation (publish span_id this subscribe was matched to)
    pub correlated_publish_span_id: Option<String>,
    // Fan-in span links from the original TraceEvent
    pub span_links: Vec<String>,
}

/// OTel span kind.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpanKind {
    Producer,
    Consumer,
    Server,
    Internal,
}

impl SpanKind {
    pub fn as_str(&self) -> &'static str {
        match self {
            SpanKind::Producer => "PRODUCER",
            SpanKind::Consumer => "CONSUMER",
            SpanKind::Server => "SERVER",
            SpanKind::Internal => "INTERNAL",
        }
    }
}

impl OtelSpan {
    /// Build an `OtelSpan` from a reconstructed span, with an optional
    /// cross-process correlation link to a publish span.
    pub fn from_span(span: ReconstructedSpan, correlated_publish_span_id: Option<String>) -> Self {
        let kind = span_kind_for(&span.operation);
        let span_name = format!("{} {}", span.operation, span.topic_or_service);
        let parent = if span.parent_span_id.is_empty() {
            None
        } else {
            Some(span.parent_span_id.clone())
        };
        let topic = if span.topic_or_service.is_empty() {
            None
        } else {
            Some(span.topic_or_service.clone())
        };

        OtelSpan {
            trace_id: span.trace_id,
            span_id: span.span_id,
            parent_span_id: parent,
            operation: span.operation,
            span_name,
            span_kind: kind,
            start_time_ns: span.start_time_ns,
            end_time_ns: span.end_time_ns,
            duration_ns: span.duration_ns,
            status_code: "OK".to_string(),
            ros_topic: topic,
            ros_node_name: span.node_name,
            ros_node_namespace: span.node_namespace,
            ros_message_type: span.message_type,
            ros_publisher_gid: span.publisher_gid,
            ros_content_hash: span.content_hash,
            ros_sequence_number: span.sequence_number,
            ros_source_timestamp_ns: span.source_timestamp_ns,
            ros_message_size_bytes: span.message_size_bytes,
            ros_dds_domain_id: span.dds_domain_id,
            ros_correlation_method: span.correlation_method,
            correlated_publish_span_id,
            span_links: span.span_links,
        }
    }
}

fn span_kind_for(operation: &str) -> SpanKind {
    match operation {
        "publish" => SpanKind::Producer,
        "subscribe" => SpanKind::Consumer,
        "service" => SpanKind::Server,
        _ => SpanKind::Internal,
    }
}

// ---------------------------------------------------------------------------
// OTel log
// ---------------------------------------------------------------------------

/// OTel-compatible log record for database export.
#[derive(Debug, Clone)]
pub struct OtelLog {
    pub timestamp_ns: i64,
    pub severity: String,
    pub severity_number: i32,
    pub body: String,
    pub logger_name: String,
    // Trace correlation (present when log was emitted during a traced callback)
    pub trace_id: Option<String>,
    pub span_id: Option<String>,
    // Source location
    pub code_filepath: Option<String>,
    pub code_function: Option<String>,
    pub code_lineno: Option<u32>,
}

// ---------------------------------------------------------------------------
// Export record — unified enum for the export channel
// ---------------------------------------------------------------------------

/// Sent through the export channel from subscribers to the exporter task.
pub enum ExportRecord {
    Span(OtelSpan),
    Log(OtelLog),
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pipeline::span_reconstructor::ReconstructedSpan;

    fn make_span(operation: &str) -> ReconstructedSpan {
        ReconstructedSpan {
            trace_id: "trace-1".into(),
            span_id: "span-1".into(),
            parent_span_id: "parent-1".into(),
            span_links: vec![],
            operation: operation.into(),
            topic_or_service: "/test_topic".into(),
            node_name: "node".into(),
            node_namespace: "/".into(),
            message_type: "std_msgs/msg/String".into(),
            publisher_gid: "aabbcc".into(),
            content_hash: 0xdeadbeef,
            sequence_number: 1,
            source_timestamp_ns: 1_000_000_000,
            message_size_bytes: 100,
            dds_domain_id: 0,
            correlation_method: 2,
            start_time_ns: 1_000_000_000,
            end_time_ns: 1_000_100_000,
            duration_ns: 100_000,
        }
    }

    #[test]
    fn test_publish_span_kind() {
        let otel = OtelSpan::from_span(make_span("publish"), None);
        assert_eq!(otel.span_kind, SpanKind::Producer);
        assert_eq!(otel.span_name, "publish /test_topic");
        assert_eq!(otel.parent_span_id, Some("parent-1".to_string()));
    }

    #[test]
    fn test_subscribe_span_kind() {
        let otel = OtelSpan::from_span(make_span("subscribe"), Some("pub-span-123".into()));
        assert_eq!(otel.span_kind, SpanKind::Consumer);
        assert_eq!(
            otel.correlated_publish_span_id,
            Some("pub-span-123".to_string())
        );
    }

    #[test]
    fn test_service_span_kind() {
        let otel = OtelSpan::from_span(make_span("service"), None);
        assert_eq!(otel.span_kind, SpanKind::Server);
    }

    #[test]
    fn test_empty_parent_becomes_none() {
        let mut span = make_span("publish");
        span.parent_span_id = String::new();
        let otel = OtelSpan::from_span(span, None);
        assert_eq!(otel.parent_span_id, None);
    }
}
