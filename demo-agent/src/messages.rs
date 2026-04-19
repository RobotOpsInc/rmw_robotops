//! CDR-compatible message structs for ROS2 topics.
//!
//! These structs mirror the wire format of `robotops_msgs` and `rcl_interfaces`
//! messages exactly. Field declaration order MUST match the `.msg` file order
//! because CDR serialization is position-dependent.
//!
//! # Drift Protection
//!
//! These structs are hand-written to avoid a dependency on `robotops-msgs`
//! (which requires rosidl C libraries). A CI test in this crate verifies
//! CDR round-trip fidelity against the expected field layout.
//!
//! If `robotops_msgs` message definitions change (breaking change, major version
//! bump), update the structs here and the corresponding constants.

use serde::Deserialize;

// ---------------------------------------------------------------------------
// Common
// ---------------------------------------------------------------------------

/// CDR-compatible `builtin_interfaces/Time`.
#[derive(Debug, Clone, Default, Deserialize)]
#[cfg_attr(test, derive(serde::Serialize))]
pub struct CdrTime {
    pub sec: i32,
    pub nanosec: u32,
}

impl CdrTime {
    /// Convert to nanoseconds since epoch.
    pub fn to_ns(&self) -> i64 {
        (self.sec as i64) * 1_000_000_000 + (self.nanosec as i64)
    }
}

// ---------------------------------------------------------------------------
// TraceEvent  (robotops_msgs/msg/TraceEvent)
//
// Field order matches msg/TraceEvent.msg exactly for CDR deserialization.
// ---------------------------------------------------------------------------

/// CDR-compatible `robotops_msgs/msg/TraceEvent`.
// msg_ptr is required by the CDR wire format (position-dependent) but not read by this agent.
#[allow(dead_code)]
#[derive(Debug, Clone, Default, Deserialize)]
#[cfg_attr(test, derive(serde::Serialize))]
pub struct TraceEvent {
    pub timestamp: CdrTime,
    pub event_type: u8,
    pub trace_id: String,
    pub span_id: String,
    pub parent_span_id: String,
    pub span_links: Vec<String>,
    pub topic_or_service: String,
    pub node_name: String,
    pub node_namespace: String,
    pub publisher_gid: String,
    pub sequence_number: u64,
    pub source_timestamp_ns: i64,
    pub content_hash: u64,
    pub msg_ptr: u64,
    pub message_type: String,
    pub message_size_bytes: u32,
    pub dds_domain_id: u32,
    pub correlation_method: u8,
}

#[allow(dead_code)]
impl TraceEvent {
    pub const EVENT_PUBLISH_RMW_START: u8 = 1;
    pub const EVENT_PUBLISH_RMW_END: u8 = 2;
    pub const EVENT_TAKE_RMW_START: u8 = 3;
    pub const EVENT_TAKE_RMW_END: u8 = 4;
    pub const EVENT_SERVICE_REQUEST: u8 = 5;
    pub const EVENT_SERVICE_RESPONSE: u8 = 6;
    pub const EVENT_ACTION_GOAL: u8 = 7;
    pub const EVENT_ACTION_FEEDBACK: u8 = 8;
    pub const EVENT_ACTION_RESULT: u8 = 9;
    pub const EVENT_ACTION_CANCEL: u8 = 10;

    pub const CORRELATION_FASTDDS_SEQUENCE: u8 = 1;
    pub const CORRELATION_FALLBACK_HASH: u8 = 2;
    pub const CORRELATION_FALLBACK_TIMESTAMP: u8 = 3;
}

// ---------------------------------------------------------------------------
// TraceContextChange  (robotops_msgs/msg/TraceContextChange)
//
// Field order matches msg/TraceContextChange.msg exactly.
// ---------------------------------------------------------------------------

/// CDR-compatible `robotops_msgs/msg/TraceContextChange`.
// timestamp and thread_id are required by the CDR wire format but not read by this agent.
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize)]
#[cfg_attr(test, derive(serde::Serialize))]
pub struct TraceContextChange {
    pub timestamp: CdrTime,
    pub node_name: String,
    pub node_namespace: String,
    pub thread_id: u64,
    pub trace_id: String,
    pub span_id: String,
    pub change_type: u8,
}

impl TraceContextChange {
    pub const CONTEXT_ENTERED: u8 = 1;
    pub const CONTEXT_EXITED: u8 = 2;
}

// ---------------------------------------------------------------------------
// RosoutLog  (rcl_interfaces/msg/Log)
//
// Field order matches the ROS2 Log.msg definition exactly.
// ---------------------------------------------------------------------------

/// CDR-compatible `rcl_interfaces/msg/Log`.
#[derive(Debug, Clone, Deserialize)]
#[cfg_attr(test, derive(serde::Serialize))]
pub struct RosoutLog {
    pub stamp: CdrTime,
    pub level: u8,
    pub name: String,
    pub msg: String,
    pub file: String,
    pub function: String,
    pub line: u32,
}

impl RosoutLog {
    /// Map ROS2 log level to OTel severity string and number.
    pub fn severity(&self) -> (&'static str, i32) {
        match self.level {
            10 => ("DEBUG", 5),
            20 => ("INFO", 9),
            30 => ("WARN", 13),
            40 => ("ERROR", 17),
            50 => ("FATAL", 21),
            _ => ("INFO", 9),
        }
    }
}

// ---------------------------------------------------------------------------
// Tests — CDR round-trip fidelity (drift protection)
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn trace_event_cdr_round_trip() {
        let event = TraceEvent {
            timestamp: CdrTime {
                sec: 1234567890,
                nanosec: 500_000_000,
            },
            event_type: TraceEvent::EVENT_PUBLISH_RMW_START,
            trace_id: "aabbccdd00112233aabbccdd00112233".to_string(),
            span_id: "0011223344556677".to_string(),
            parent_span_id: String::new(),
            span_links: vec!["aabbccdd00112233aabbccdd00112233:0011223344556677".to_string()],
            topic_or_service: "/test_topic".to_string(),
            node_name: "test_node".to_string(),
            node_namespace: "/".to_string(),
            publisher_gid: "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4".to_string(),
            sequence_number: 42,
            source_timestamp_ns: 1_234_567_890_000,
            content_hash: 0xdeadbeef_cafebabe,
            msg_ptr: 0,
            message_type: "std_msgs/msg/String".to_string(),
            message_size_bytes: 128,
            dds_domain_id: 0,
            correlation_method: TraceEvent::CORRELATION_FALLBACK_HASH,
        };

        let serialized = cdr::serialize::<_, _, cdr::CdrLe>(&event, cdr::Infinite).unwrap();
        let deserialized: TraceEvent = cdr::deserialize(&serialized).unwrap();

        assert_eq!(deserialized.timestamp.sec, 1234567890);
        assert_eq!(deserialized.timestamp.nanosec, 500_000_000);
        assert_eq!(deserialized.event_type, TraceEvent::EVENT_PUBLISH_RMW_START);
        assert_eq!(deserialized.trace_id, "aabbccdd00112233aabbccdd00112233");
        assert_eq!(deserialized.span_id, "0011223344556677");
        assert_eq!(deserialized.span_links.len(), 1);
        assert_eq!(deserialized.topic_or_service, "/test_topic");
        assert_eq!(
            deserialized.publisher_gid,
            "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4"
        );
        assert_eq!(deserialized.sequence_number, 42);
        assert_eq!(deserialized.content_hash, 0xdeadbeef_cafebabe);
        assert_eq!(
            deserialized.correlation_method,
            TraceEvent::CORRELATION_FALLBACK_HASH
        );
    }

    #[test]
    fn trace_context_change_cdr_round_trip() {
        let change = TraceContextChange {
            timestamp: CdrTime {
                sec: 100,
                nanosec: 0,
            },
            node_name: "talker".to_string(),
            node_namespace: "/my_ns".to_string(),
            thread_id: 12345,
            trace_id: "aabbccdd00112233aabbccdd00112233".to_string(),
            span_id: "0011223344556677".to_string(),
            change_type: TraceContextChange::CONTEXT_ENTERED,
        };

        let serialized = cdr::serialize::<_, _, cdr::CdrLe>(&change, cdr::Infinite).unwrap();
        let deserialized: TraceContextChange = cdr::deserialize(&serialized).unwrap();

        assert_eq!(deserialized.node_name, "talker");
        assert_eq!(deserialized.node_namespace, "/my_ns");
        assert_eq!(deserialized.thread_id, 12345);
        assert_eq!(deserialized.trace_id, "aabbccdd00112233aabbccdd00112233");
        assert_eq!(
            deserialized.change_type,
            TraceContextChange::CONTEXT_ENTERED
        );
    }

    #[test]
    fn rosout_log_cdr_round_trip() {
        let log = RosoutLog {
            stamp: CdrTime {
                sec: 1000,
                nanosec: 0,
            },
            level: 20,
            name: "my_ns.talker".to_string(),
            msg: "Hello world".to_string(),
            file: "talker.cpp".to_string(),
            function: "main".to_string(),
            line: 42,
        };

        let serialized = cdr::serialize::<_, _, cdr::CdrLe>(&log, cdr::Infinite).unwrap();
        let deserialized: RosoutLog = cdr::deserialize(&serialized).unwrap();

        assert_eq!(deserialized.level, 20);
        assert_eq!(deserialized.name, "my_ns.talker");
        assert_eq!(deserialized.msg, "Hello world");
        assert_eq!(deserialized.line, 42);
    }

    #[test]
    fn cdr_time_to_ns() {
        let t = CdrTime {
            sec: 1,
            nanosec: 500_000_000,
        };
        assert_eq!(t.to_ns(), 1_500_000_000);
    }
}
