//! Cross-process correlation engine for matching publish and subscribe spans.
//!
//! Maintains a sliding window of completed publish spans and matches incoming
//! subscribe spans using the key: `(topic, content_hash, publisher_gid, timestamp_bucket)`.
//!
//! When a match is found the publish span's `span_id` is returned so it can be
//! attached as a span link on the subscriber side.
//!
//! Ported from robot_agent's `src/distributed_tracing/correlation_engine.rs`
//! with the correlation key extended to include `publisher_gid` as specified
//! by the rmw_robotops correlation strategy.

use std::collections::HashMap;
use std::time::{Duration, Instant};
use tracing::debug;

/// Configuration for the correlation engine.
#[derive(Debug, Clone)]
pub struct CorrelationConfig {
    /// Maximum time delta between publish and subscribe timestamps (nanoseconds).
    pub timestamp_tolerance_ns: u64,
    /// How long to keep unmatched publish events in the window (seconds).
    pub window_secs: u32,
    /// Whether to use content hash in the correlation key.
    pub hash_enabled: bool,
}

impl Default for CorrelationConfig {
    fn default() -> Self {
        Self {
            timestamp_tolerance_ns: 10_000_000, // 10ms
            window_secs: 30,
            hash_enabled: true,
        }
    }
}

/// Correlation key: (topic, content_hash, publisher_gid, timestamp_bucket).
///
/// The timestamp is quantized to tolerance buckets for fuzzy matching.
/// `content_hash` and `publisher_gid` are set to sentinel values when disabled.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
struct CorrelationKey {
    topic: String,
    content_hash: u64,
    publisher_gid: String,
    timestamp_bucket: u64,
}

/// A publish span stored in the correlation window.
struct PublishEntry {
    span_id: String,
    timestamp_ns: u64,
    recorded_at: Instant,
}

/// Matches publish and subscribe spans across process boundaries.
pub struct CorrelationEngine {
    config: CorrelationConfig,
    window: HashMap<CorrelationKey, PublishEntry>,
    window_duration: Duration,
}

impl CorrelationEngine {
    pub fn new(config: CorrelationConfig) -> Self {
        let window_duration = Duration::from_secs(config.window_secs as u64);
        Self {
            config,
            window: HashMap::new(),
            window_duration,
        }
    }

    /// Record a completed publish span for future correlation.
    pub fn record_publish(
        &mut self,
        topic: String,
        source_timestamp_ns: i64,
        content_hash: u64,
        publisher_gid: String,
        span_id: String,
    ) {
        let key = self.make_key(
            &topic,
            source_timestamp_ns as u64,
            content_hash,
            &publisher_gid,
        );
        self.window.insert(
            key,
            PublishEntry {
                span_id,
                timestamp_ns: source_timestamp_ns as u64,
                recorded_at: Instant::now(),
            },
        );
    }

    /// Attempt to correlate a subscribe span with a previously recorded publish.
    ///
    /// Returns `Some(publish_span_id)` when a match is found and removes it
    /// from the window (each publish can only correlate once).
    pub fn correlate_subscribe(
        &mut self,
        topic: &str,
        source_timestamp_ns: i64,
        content_hash: u64,
        publisher_gid: &str,
    ) -> Option<String> {
        let key = self.make_key(
            topic,
            source_timestamp_ns as u64,
            content_hash,
            publisher_gid,
        );

        if let Some(entry) = self.window.remove(&key) {
            debug!(topic = topic, "Cross-process correlation matched");
            return Some(entry.span_id);
        }

        // Fallback: fuzzy timestamp search when hash matching is disabled
        if !self.config.hash_enabled {
            return self.search_with_tolerance(topic, source_timestamp_ns as u64);
        }

        None
    }

    /// Remove publish entries older than the configured window.
    pub fn cleanup_expired(&mut self) {
        let now = Instant::now();
        self.window
            .retain(|_, entry| now.duration_since(entry.recorded_at) < self.window_duration);
    }

    /// Current number of publish events in the correlation window (used in tests).
    #[cfg(test)]
    pub fn window_size(&self) -> usize {
        self.window.len()
    }

    fn make_key(
        &self,
        topic: &str,
        timestamp_ns: u64,
        content_hash: u64,
        publisher_gid: &str,
    ) -> CorrelationKey {
        CorrelationKey {
            topic: topic.to_string(),
            content_hash: if self.config.hash_enabled {
                content_hash
            } else {
                0
            },
            publisher_gid: publisher_gid.to_string(),
            timestamp_bucket: timestamp_ns / self.config.timestamp_tolerance_ns,
        }
    }

    fn search_with_tolerance(&mut self, topic: &str, timestamp_ns: u64) -> Option<String> {
        let tolerance = self.config.timestamp_tolerance_ns;
        let mut best: Option<(CorrelationKey, u64)> = None;

        for (key, entry) in &self.window {
            if key.topic != topic {
                continue;
            }
            let delta = timestamp_ns.abs_diff(entry.timestamp_ns);
            if delta <= tolerance {
                match best {
                    None => best = Some((key.clone(), delta)),
                    Some((_, current_delta)) if delta < current_delta => {
                        best = Some((key.clone(), delta));
                    }
                    _ => {}
                }
            }
        }

        best.and_then(|(key, _)| self.window.remove(&key).map(|e| e.span_id))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn config() -> CorrelationConfig {
        CorrelationConfig {
            timestamp_tolerance_ns: 10_000_000, // 10ms
            window_secs: 30,
            hash_enabled: true,
        }
    }

    #[test]
    fn test_exact_match() {
        let mut engine = CorrelationEngine::new(config());
        engine.record_publish(
            "/topic".into(),
            1_000_000_000,
            0xabc,
            "gid-1".into(),
            "span-pub".into(),
        );
        let result = engine.correlate_subscribe("/topic", 1_000_000_000, 0xabc, "gid-1");
        assert_eq!(result, Some("span-pub".to_string()));
        assert_eq!(engine.window_size(), 0);
    }

    #[test]
    fn test_no_match_different_topic() {
        let mut engine = CorrelationEngine::new(config());
        engine.record_publish(
            "/a".into(),
            1_000_000_000,
            0xabc,
            "gid-1".into(),
            "span-pub".into(),
        );
        assert!(engine
            .correlate_subscribe("/b", 1_000_000_000, 0xabc, "gid-1")
            .is_none());
    }

    #[test]
    fn test_no_match_different_gid() {
        let mut engine = CorrelationEngine::new(config());
        engine.record_publish(
            "/topic".into(),
            1_000_000_000,
            0xabc,
            "gid-1".into(),
            "span-pub".into(),
        );
        assert!(engine
            .correlate_subscribe("/topic", 1_000_000_000, 0xabc, "gid-2")
            .is_none());
    }

    #[test]
    fn test_no_match_different_hash() {
        let mut engine = CorrelationEngine::new(config());
        engine.record_publish(
            "/topic".into(),
            1_000_000_000,
            0xabc,
            "gid-1".into(),
            "span-pub".into(),
        );
        assert!(engine
            .correlate_subscribe("/topic", 1_000_000_000, 0xdef, "gid-1")
            .is_none());
    }

    #[test]
    fn test_cleanup_expired() {
        let mut cfg = config();
        cfg.window_secs = 0;
        let mut engine = CorrelationEngine::new(cfg);
        engine.record_publish(
            "/topic".into(),
            1_000_000_000,
            0xabc,
            "gid-1".into(),
            "span-pub".into(),
        );
        std::thread::sleep(Duration::from_millis(10));
        engine.cleanup_expired();
        assert_eq!(engine.window_size(), 0);
    }

    #[test]
    fn test_fuzzy_match_hash_disabled() {
        let mut cfg = config();
        cfg.hash_enabled = false;
        let mut engine = CorrelationEngine::new(cfg);
        // Publish at 1000ms, subscribe at 1005ms (within 10ms tolerance)
        engine.record_publish(
            "/topic".into(),
            1_000_000_000,
            0,
            "".into(),
            "span-pub".into(),
        );
        let result = engine.correlate_subscribe("/topic", 1_005_000_000, 0, "");
        assert_eq!(result, Some("span-pub".to_string()));
    }
}
