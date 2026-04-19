//! Trace context registry for log-to-trace correlation.
//!
//! Maintains a mapping from ROS2 node logger names to their currently active
//! trace context (trace_id + span_id), updated by TraceContextChange messages
//! from `/robotops/trace_context`.
//!
//! The registry is keyed by the ROS2 logger name format used in `/rosout`
//! `Log.name` field — dot-separated, matching `build_logger_name()` output.
//!
//! Ported from robot_agent's `src/logging/trace_context.rs`.

use crate::messages::TraceContextChange;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;
use tracing::{debug, warn};

/// Build the ROS2 logger name from node namespace and node name.
///
/// ROS2 logger names use dot-separated format:
/// - `("/", "talker")` → `"talker"`
/// - `("/my_ns", "talker")` → `"my_ns.talker"`
/// - `("/a/b", "talker")` → `"a.b.talker"`
///
/// This matches the `name` field in `/rosout` `rcl_interfaces/msg/Log` messages.
pub fn build_logger_name(namespace: &str, node_name: &str) -> String {
    let trimmed = namespace.trim_start_matches('/');
    if trimmed.is_empty() {
        node_name.to_string()
    } else {
        format!("{}.{}", trimmed.replace('/', "."), node_name)
    }
}

/// Active trace context for a ROS2 node.
#[derive(Debug, Clone)]
pub struct TraceContext {
    pub trace_id: String,
    pub span_id: String,
}

/// Thread-safe registry mapping ROS2 logger names to active trace contexts.
#[derive(Default)]
pub struct TraceContextRegistry {
    contexts: RwLock<HashMap<String, TraceContext>>,
}

impl TraceContextRegistry {
    pub fn new() -> Self {
        Self::default()
    }

    /// Register an active trace context for the given logger name.
    pub fn register_context(&self, logger_name: String, trace_id: String, span_id: String) {
        let mut contexts = self.contexts.write();
        contexts.insert(logger_name, TraceContext { trace_id, span_id });
    }

    /// Remove the trace context for the given logger name.
    pub fn remove_context(&self, logger_name: &str) {
        let mut contexts = self.contexts.write();
        contexts.remove(logger_name);
    }

    /// Look up the active trace context for the given logger name.
    pub fn get_context(&self, logger_name: &str) -> Option<TraceContext> {
        let contexts = self.contexts.read();
        contexts.get(logger_name).cloned()
    }
}

/// Handle a TraceContextChange event, updating the registry accordingly.
pub fn handle_context_change(event: &TraceContextChange, registry: &Arc<TraceContextRegistry>) {
    let logger_name = build_logger_name(&event.node_namespace, &event.node_name);

    match event.change_type {
        TraceContextChange::CONTEXT_ENTERED => {
            debug!(
                logger_name = %logger_name,
                trace_id = %event.trace_id,
                span_id = %event.span_id,
                "Trace context entered"
            );
            registry.register_context(logger_name, event.trace_id.clone(), event.span_id.clone());
        }
        TraceContextChange::CONTEXT_EXITED => {
            debug!(logger_name = %logger_name, "Trace context exited");
            registry.remove_context(&logger_name);
        }
        _ => {
            warn!(
                change_type = event.change_type,
                node = %event.node_name,
                "Unknown trace context change type"
            );
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_build_logger_name_root_namespace() {
        assert_eq!(build_logger_name("/", "talker"), "talker");
        assert_eq!(build_logger_name("", "talker"), "talker");
    }

    #[test]
    fn test_build_logger_name_single_namespace() {
        assert_eq!(build_logger_name("/my_ns", "talker"), "my_ns.talker");
    }

    #[test]
    fn test_build_logger_name_nested_namespace() {
        assert_eq!(build_logger_name("/a/b", "talker"), "a.b.talker");
        assert_eq!(build_logger_name("/a/b/c", "node"), "a.b.c.node");
    }

    #[test]
    fn test_registry_register_and_get() {
        let registry = TraceContextRegistry::new();
        registry.register_context("test_node".into(), "trace-123".into(), "span-456".into());

        let ctx = registry.get_context("test_node").unwrap();
        assert_eq!(ctx.trace_id, "trace-123");
        assert_eq!(ctx.span_id, "span-456");
    }

    #[test]
    fn test_registry_remove() {
        let registry = TraceContextRegistry::new();
        registry.register_context("test_node".into(), "trace-123".into(), "span-456".into());
        registry.remove_context("test_node");
        assert!(registry.get_context("test_node").is_none());
    }

    #[test]
    fn test_handle_context_entered() {
        let registry = Arc::new(TraceContextRegistry::new());
        let event = TraceContextChange {
            timestamp: crate::messages::CdrTime::default(),
            node_name: "talker".into(),
            node_namespace: "/my_ns".into(),
            thread_id: 0,
            trace_id: "trace-abc".into(),
            span_id: "span-def".into(),
            change_type: TraceContextChange::CONTEXT_ENTERED,
        };
        handle_context_change(&event, &registry);
        assert!(registry.get_context("my_ns.talker").is_some());
    }

    #[test]
    fn test_handle_context_exited() {
        let registry = Arc::new(TraceContextRegistry::new());
        let enter = TraceContextChange {
            timestamp: crate::messages::CdrTime::default(),
            node_name: "talker".into(),
            node_namespace: "/".into(),
            thread_id: 0,
            trace_id: "trace-abc".into(),
            span_id: "span-def".into(),
            change_type: TraceContextChange::CONTEXT_ENTERED,
        };
        handle_context_change(&enter, &registry);
        assert!(registry.get_context("talker").is_some());

        let exit = TraceContextChange {
            change_type: TraceContextChange::CONTEXT_EXITED,
            ..enter
        };
        handle_context_change(&exit, &registry);
        assert!(registry.get_context("talker").is_none());
    }
}
