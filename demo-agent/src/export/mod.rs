//! Span and log exporters.
//!
//! The `SpanExporter` trait is implemented by each backend. The factory
//! function `create_exporter` parses the output URI and returns the appropriate
//! implementation.

use crate::error::{BridgeError, Result};
use crate::pipeline::otel_builder::{OtelLog, OtelSpan};

pub mod duckdb;

#[cfg(feature = "postgres")]
pub mod postgres;

#[cfg(feature = "otlp")]
pub mod otlp;

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

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

/// Parse `output_uri` and construct the appropriate exporter.
pub fn create_exporter(output_uri: &str) -> Result<Box<dyn SpanExporter>> {
    let scheme = output_uri
        .find("://")
        .map(|pos| &output_uri[..pos])
        .unwrap_or("duckdb");

    match scheme {
        "duckdb" => {
            #[cfg(feature = "duckdb-output")]
            {
                let path = parse_duckdb_path(output_uri)?;
                let exporter = duckdb::DuckDbExporter::open(&path)?;
                Ok(Box::new(exporter))
            }
            #[cfg(not(feature = "duckdb-output"))]
            {
                Err(BridgeError::FeatureNotEnabled("duckdb-output".into()))
            }
        }

        "postgres" | "postgresql" => {
            #[cfg(feature = "postgres")]
            {
                Err(BridgeError::InvalidUri(
                    output_uri.to_string(),
                    "PostgreSQL exporter is not yet implemented in this release".into(),
                ))
            }
            #[cfg(not(feature = "postgres"))]
            {
                Err(BridgeError::FeatureNotEnabled("postgres".into()))
            }
        }

        "otlp" => {
            #[cfg(feature = "otlp")]
            {
                Err(BridgeError::InvalidUri(
                    output_uri.to_string(),
                    "OTLP exporter is not yet implemented in this release".into(),
                ))
            }
            #[cfg(not(feature = "otlp"))]
            {
                Err(BridgeError::FeatureNotEnabled("otlp".into()))
            }
        }

        other => Err(BridgeError::InvalidUri(
            output_uri.to_string(),
            format!(
                "unknown scheme '{}' — use duckdb://, postgres://, or otlp://",
                other
            ),
        )),
    }
}

/// Extract the file path from a `duckdb:///path` URI.
fn parse_duckdb_path(uri: &str) -> Result<String> {
    // duckdb:///absolute/path → /absolute/path
    // duckdb://relative/path → relative/path
    let without_scheme = uri.strip_prefix("duckdb://").ok_or_else(|| {
        BridgeError::InvalidUri(uri.to_string(), "expected duckdb:// prefix".into())
    })?;
    // Three slashes: duckdb:///foo → /foo  (empty host, absolute path)
    // Two slashes:   duckdb://foo  → foo   (relative path)
    let path = without_scheme.trim_start_matches('/');
    if path.is_empty() {
        // In-memory database
        Ok(":memory:".to_string())
    } else if without_scheme.starts_with('/') {
        // Absolute path: restore the leading slash
        Ok(format!("/{}", path))
    } else {
        Ok(path.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_duckdb_path_absolute() {
        assert_eq!(
            parse_duckdb_path("duckdb:///tmp/foo.db").unwrap(),
            "/tmp/foo.db"
        );
    }

    #[test]
    fn test_parse_duckdb_path_relative() {
        assert_eq!(parse_duckdb_path("duckdb://foo.db").unwrap(), "foo.db");
    }

    #[test]
    fn test_parse_duckdb_path_memory() {
        assert_eq!(parse_duckdb_path("duckdb://").unwrap(), ":memory:");
    }
}
