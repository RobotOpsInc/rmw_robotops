//! Span and log exporters.
//!
//! The `SpanExporter` trait is implemented by each backend. Use `create_exporter`
//! to construct the appropriate implementation from an output path.

use crate::error::Result;
use crate::pipeline::otel_builder::{OtelLog, OtelSpan};

pub mod parquet;

// ---------------------------------------------------------------------------
// Exporter trait
// ---------------------------------------------------------------------------

/// Synchronous exporter trait implemented by each output backend.
pub trait SpanExporter: Send {
    fn export_span(&mut self, span: &OtelSpan) -> Result<()>;
    fn export_log(&mut self, log: &OtelLog) -> Result<()>;
    /// Flush any pending buffered writes to the underlying store.
    fn flush(&mut self) -> Result<()>;
}
