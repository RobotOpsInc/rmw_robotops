//! DuckDB exporter — the default output backend.
//!
//! Writes OTel-compatible `otel_traces` and `otel_logs` tables into an
//! embedded DuckDB database. No external database server required.
//!
//! Schema is designed to match what ROSQL expects for trace and log queries,
//! with dedicated columns for all ROS2-specific attributes plus a JSON
//! `attributes` column for extensibility.

use crate::error::Result;
use crate::export::SpanExporter;
use crate::pipeline::otel_builder::{OtelLog, OtelSpan};
use duckdb::Connection;
use tracing::{debug, warn};

/// DuckDB-backed span and log exporter.
pub struct DuckDbExporter {
    conn: Connection,
    pending_spans: Vec<OtelSpan>,
    pending_logs: Vec<OtelLog>,
    /// Flush when either buffer reaches this size.
    batch_size: usize,
}

impl DuckDbExporter {
    /// Open (or create) a DuckDB database at the given path.
    ///
    /// Pass `":memory:"` for an in-memory database.
    pub fn open(path: &str) -> Result<Self> {
        let conn = if path == ":memory:" {
            Connection::open_in_memory()?
        } else {
            Connection::open(path)?
        };

        let exporter = Self {
            conn,
            pending_spans: Vec::new(),
            pending_logs: Vec::new(),
            batch_size: 100,
        };

        exporter.create_schema()?;
        Ok(exporter)
    }

    fn create_schema(&self) -> Result<()> {
        self.conn.execute_batch(
            "
            CREATE TABLE IF NOT EXISTS otel_traces (
                trace_id                    VARCHAR NOT NULL,
                span_id                     VARCHAR NOT NULL,
                parent_span_id              VARCHAR,
                operation                   VARCHAR NOT NULL,
                span_name                   VARCHAR NOT NULL,
                span_kind                   VARCHAR NOT NULL,
                start_time_ns               BIGINT  NOT NULL,
                end_time_ns                 BIGINT  NOT NULL,
                duration_ns                 BIGINT  NOT NULL,
                status_code                 VARCHAR NOT NULL DEFAULT 'OK',
                ros_topic                   VARCHAR,
                ros_node_name               VARCHAR,
                ros_node_namespace          VARCHAR,
                ros_message_type            VARCHAR,
                ros_publisher_gid           VARCHAR,
                ros_content_hash            UBIGINT,
                ros_sequence_number         UBIGINT,
                ros_source_timestamp_ns     BIGINT,
                ros_message_size_bytes      UINTEGER,
                ros_dds_domain_id           UINTEGER,
                ros_correlation_method      UTINYINT,
                correlated_publish_span_id  VARCHAR,
                attributes                  JSON,
                span_links                  JSON,
                inserted_at                 TIMESTAMP DEFAULT current_timestamp
            );

            CREATE INDEX IF NOT EXISTS idx_traces_trace_id
                ON otel_traces (trace_id);
            CREATE INDEX IF NOT EXISTS idx_traces_topic
                ON otel_traces (ros_topic);
            CREATE INDEX IF NOT EXISTS idx_traces_start_time
                ON otel_traces (start_time_ns);

            CREATE TABLE IF NOT EXISTS otel_logs (
                timestamp_ns        BIGINT  NOT NULL,
                severity            VARCHAR NOT NULL,
                severity_number     INTEGER NOT NULL,
                body                VARCHAR NOT NULL,
                logger_name         VARCHAR,
                trace_id            VARCHAR,
                span_id             VARCHAR,
                code_filepath       VARCHAR,
                code_function       VARCHAR,
                code_lineno         INTEGER,
                attributes          JSON,
                inserted_at         TIMESTAMP DEFAULT current_timestamp
            );

            CREATE INDEX IF NOT EXISTS idx_logs_timestamp
                ON otel_logs (timestamp_ns);
            CREATE INDEX IF NOT EXISTS idx_logs_trace_id
                ON otel_logs (trace_id);
        ",
        )?;

        debug!("DuckDB schema initialized");
        Ok(())
    }

    fn flush_spans(&mut self) -> Result<()> {
        if self.pending_spans.is_empty() {
            return Ok(());
        }

        let mut appender = self.conn.appender("otel_traces")?;
        for span in self.pending_spans.drain(..) {
            let span_links_json =
                serde_json::to_string(&span.span_links).unwrap_or_else(|_| "[]".to_string());

            appender.append_row(duckdb::params![
                span.trace_id,
                span.span_id,
                span.parent_span_id,
                span.operation,
                span.span_name,
                span.span_kind.as_str(),
                span.start_time_ns,
                span.end_time_ns,
                span.duration_ns,
                span.status_code,
                span.ros_topic,
                span.ros_node_name,
                span.ros_node_namespace,
                span.ros_message_type,
                span.ros_publisher_gid,
                span.ros_content_hash,
                span.ros_sequence_number,
                span.ros_source_timestamp_ns,
                span.ros_message_size_bytes,
                span.ros_dds_domain_id,
                span.ros_correlation_method,
                span.correlated_publish_span_id,
                Option::<String>::None, // attributes (future use)
                span_links_json,
            ])?;
        }
        appender.flush()?;
        debug!("Flushed span batch to DuckDB");
        Ok(())
    }

    fn flush_logs(&mut self) -> Result<()> {
        if self.pending_logs.is_empty() {
            return Ok(());
        }

        let mut appender = self.conn.appender("otel_logs")?;
        for log in self.pending_logs.drain(..) {
            appender.append_row(duckdb::params![
                log.timestamp_ns,
                log.severity,
                log.severity_number,
                log.body,
                log.logger_name,
                log.trace_id,
                log.span_id,
                log.code_filepath,
                log.code_function,
                log.code_lineno.map(|l| l as i32),
                Option::<String>::None, // attributes (future use)
            ])?;
        }
        appender.flush()?;
        debug!("Flushed log batch to DuckDB");
        Ok(())
    }
}

impl SpanExporter for DuckDbExporter {
    fn export_span(&mut self, span: &OtelSpan) -> Result<()> {
        self.pending_spans.push(span.clone());
        if self.pending_spans.len() >= self.batch_size {
            if let Err(e) = self.flush_spans() {
                warn!("Failed to flush spans to DuckDB: {}", e);
            }
        }
        Ok(())
    }

    fn export_log(&mut self, log: &OtelLog) -> Result<()> {
        self.pending_logs.push(log.clone());
        if self.pending_logs.len() >= self.batch_size {
            if let Err(e) = self.flush_logs() {
                warn!("Failed to flush logs to DuckDB: {}", e);
            }
        }
        Ok(())
    }

    fn flush(&mut self) -> Result<()> {
        self.flush_spans()?;
        self.flush_logs()?;
        Ok(())
    }
}
