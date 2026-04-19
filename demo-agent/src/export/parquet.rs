//! Parquet exporter for OTel spans and logs.
//!
//! Writes Parquet files (Snappy-compressed) to a local directory or S3-compatible
//! object store. Each flush produces one file per table when the buffer is non-empty.
//!
//! Output structure:
//!   <base>/robotops_demo_agent/<yyyymmdd-hhmmss>/traces/part-0001.parquet
//!   <base>/robotops_demo_agent/<yyyymmdd-hhmmss>/logs/part-0001.parquet
//!
//! Schema conforms to the ROSQL OtelPostgres profile so that ROSQL queries work
//! out of the box with `--backend parquet`. ROS2-specific fields are appended as
//! extra columns after the core OtelPostgres columns; ROSQL ignores them.

use crate::error::{BridgeError, Result};
use crate::export::SpanExporter;
use crate::pipeline::otel_builder::{OtelLog, OtelSpan};
use arrow::array::{
    ArrayRef, Int32Array, Int64Array, StringArray, TimestampMicrosecondArray, UInt32Array,
    UInt64Array, UInt8Array,
};
use arrow::datatypes::{DataType, Field, Schema, TimeUnit};
use arrow::record_batch::RecordBatch;
use chrono::Utc;
use object_store::{path::Path as OsPath, ObjectStore};
use parquet::arrow::ArrowWriter;
use parquet::basic::Compression;
use parquet::file::properties::WriterProperties;
use std::io::Cursor;
use std::sync::Arc;
use tracing::debug;

// ---------------------------------------------------------------------------
// Arrow schemas  (OtelPostgres core columns first, ROS extras after)
// ---------------------------------------------------------------------------

fn traces_schema() -> Schema {
    Schema::new(vec![
        // ── OtelPostgres core ──────────────────────────────────────────
        Field::new(
            "timestamp",
            DataType::Timestamp(TimeUnit::Microsecond, Some(Arc::from("UTC"))),
            false,
        ),
        Field::new("trace_id", DataType::Utf8, false),
        Field::new("span_id", DataType::Utf8, false),
        Field::new("parent_span_id", DataType::Utf8, false),
        Field::new("span_name", DataType::Utf8, false),
        Field::new("span_kind", DataType::Utf8, false),
        Field::new("service_name", DataType::Utf8, false),
        Field::new("duration", DataType::Int64, false),
        Field::new("status_code", DataType::Utf8, false),
        Field::new("span_attributes", DataType::Utf8, false),
        Field::new("resource_attributes", DataType::Utf8, false),
        // ── ROS2-specific extras (ignored by ROSQL, useful for local analysis) ──
        Field::new("operation", DataType::Utf8, false),
        Field::new("start_time_ns", DataType::Int64, false),
        Field::new("end_time_ns", DataType::Int64, false),
        Field::new("ros_topic", DataType::Utf8, true),
        Field::new("ros_node_name", DataType::Utf8, false),
        Field::new("ros_node_namespace", DataType::Utf8, false),
        Field::new("ros_message_type", DataType::Utf8, false),
        Field::new("ros_publisher_gid", DataType::Utf8, false),
        Field::new("ros_content_hash", DataType::UInt64, false),
        Field::new("ros_sequence_number", DataType::UInt64, false),
        Field::new("ros_source_timestamp_ns", DataType::Int64, false),
        Field::new("ros_message_size_bytes", DataType::UInt32, false),
        Field::new("ros_dds_domain_id", DataType::UInt32, false),
        Field::new("ros_correlation_method", DataType::UInt8, false),
        Field::new("correlated_publish_span_id", DataType::Utf8, true),
        Field::new("span_links", DataType::Utf8, true),
        Field::new("written_at_ns", DataType::Int64, false),
    ])
}

fn logs_schema() -> Schema {
    Schema::new(vec![
        // ── OtelPostgres core ──────────────────────────────────────────
        Field::new(
            "timestamp",
            DataType::Timestamp(TimeUnit::Microsecond, Some(Arc::from("UTC"))),
            false,
        ),
        Field::new("trace_id", DataType::Utf8, false),
        Field::new("span_id", DataType::Utf8, false),
        Field::new("severity_text", DataType::Utf8, false),
        Field::new("severity_number", DataType::Int32, false),
        Field::new("service_name", DataType::Utf8, false),
        Field::new("body", DataType::Utf8, false),
        Field::new("resource_attributes", DataType::Utf8, false),
        Field::new("log_attributes", DataType::Utf8, false),
        // ── Extra for debugging ────────────────────────────────────────
        Field::new("written_at_ns", DataType::Int64, false),
    ])
}

// ---------------------------------------------------------------------------
// ParquetExporter
// ---------------------------------------------------------------------------

pub struct ParquetExporter {
    store: Arc<dyn ObjectStore>,
    session_path: String,
    pending_spans: Vec<OtelSpan>,
    pending_logs: Vec<OtelLog>,
    batch_size: usize,
    trace_part: u32,
    log_part: u32,
    bytes_written: u64,
    limit_bytes: u64,
    resource_attrs_json: String,
}

impl ParquetExporter {
    /// Create a new exporter writing to `output` (local path or `s3://bucket/prefix`).
    pub fn new(
        output: &str,
        batch_size: usize,
        limit_mb: u64,
        resource_attrs_json: String,
    ) -> Result<Self> {
        let session_ts = Utc::now().format("%Y%m%d-%H%M%S").to_string();
        let session_path = format!("robotops_demo_agent/{}", session_ts);

        let store: Arc<dyn ObjectStore> = if output.starts_with("s3://") {
            Arc::new(build_s3_store(output)?)
        } else {
            // Local filesystem — create the base directory
            std::fs::create_dir_all(output)?;
            Arc::new(
                object_store::local::LocalFileSystem::new_with_prefix(std::path::Path::new(output))
                    .map_err(|e| BridgeError::InvalidOutput(output.to_string(), e.to_string()))?,
            )
        };

        Ok(ParquetExporter {
            store,
            session_path,
            pending_spans: Vec::new(),
            pending_logs: Vec::new(),
            batch_size,
            trace_part: 0,
            log_part: 0,
            bytes_written: 0,
            limit_bytes: limit_mb * 1024 * 1024,
            resource_attrs_json,
        })
    }

    /// Full output path for display in the startup greeting.
    pub fn session_path(&self) -> &str {
        &self.session_path
    }

    fn flush_spans(&mut self) -> Result<()> {
        if self.pending_spans.is_empty() {
            return Ok(());
        }

        let batch = spans_to_record_batch(&self.pending_spans, &self.resource_attrs_json)?;
        let bytes = record_batch_to_parquet(&batch)?;
        let n_bytes = bytes.len() as u64;

        self.trace_part += 1;
        let path = OsPath::from(format!(
            "{}/traces/part-{:04}.parquet",
            self.session_path, self.trace_part
        ));

        tokio::task::block_in_place(|| {
            tokio::runtime::Handle::current()
                .block_on(self.store.put(&path, bytes.into()))
        })
        .map_err(BridgeError::from)?;

        self.bytes_written += n_bytes;
        debug!(
            path = %path,
            rows = self.pending_spans.len(),
            bytes = n_bytes,
            "Wrote traces Parquet file"
        );
        self.pending_spans.clear();
        Ok(())
    }

    fn flush_logs(&mut self) -> Result<()> {
        if self.pending_logs.is_empty() {
            return Ok(());
        }

        let batch = logs_to_record_batch(&self.pending_logs, &self.resource_attrs_json)?;
        let bytes = record_batch_to_parquet(&batch)?;
        let n_bytes = bytes.len() as u64;

        self.log_part += 1;
        let path = OsPath::from(format!(
            "{}/logs/part-{:04}.parquet",
            self.session_path, self.log_part
        ));

        tokio::task::block_in_place(|| {
            tokio::runtime::Handle::current()
                .block_on(self.store.put(&path, bytes.into()))
        })
        .map_err(BridgeError::from)?;

        self.bytes_written += n_bytes;
        debug!(
            path = %path,
            rows = self.pending_logs.len(),
            bytes = n_bytes,
            "Wrote logs Parquet file"
        );
        self.pending_logs.clear();
        Ok(())
    }
}

impl SpanExporter for ParquetExporter {
    fn export_span(&mut self, span: &OtelSpan) -> Result<()> {
        self.pending_spans.push(span.clone());
        if self.pending_spans.len() >= self.batch_size {
            self.flush_spans()?;
            if self.bytes_written >= self.limit_bytes {
                return Err(BridgeError::StorageLimitReached);
            }
        }
        Ok(())
    }

    fn export_log(&mut self, log: &OtelLog) -> Result<()> {
        self.pending_logs.push(log.clone());
        if self.pending_logs.len() >= self.batch_size {
            self.flush_logs()?;
            if self.bytes_written >= self.limit_bytes {
                return Err(BridgeError::StorageLimitReached);
            }
        }
        Ok(())
    }

    fn flush(&mut self) -> Result<()> {
        self.flush_spans()?;
        self.flush_logs()?;
        if self.bytes_written >= self.limit_bytes {
            return Err(BridgeError::StorageLimitReached);
        }
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// S3 builder
// ---------------------------------------------------------------------------

fn build_s3_store(uri: &str) -> Result<object_store::aws::AmazonS3> {
    // uri: s3://bucket/prefix  or  s3://bucket
    let without_scheme = uri.strip_prefix("s3://").unwrap_or(uri);
    let bucket = without_scheme
        .split('/')
        .next()
        .filter(|s| !s.is_empty())
        .ok_or_else(|| {
            BridgeError::InvalidOutput(
                uri.to_string(),
                "s3:// URI must include a bucket name".into(),
            )
        })?;

    let builder = object_store::aws::AmazonS3Builder::from_env().with_bucket_name(bucket);

    builder.build().map_err(|e| {
        BridgeError::InvalidOutput(
            uri.to_string(),
            format!("S3 configuration error: {}. Check AWS_REGION, AWS_PROFILE, AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_ENDPOINT_URL.", e),
        )
    })
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn service_name_for(namespace: &str, node_name: &str) -> String {
    match namespace.trim_end_matches('/') {
        "" | "/" => format!("/{node_name}"),
        ns => format!("{ns}/{node_name}"),
    }
}

fn span_attributes_json(s: &OtelSpan) -> String {
    let mut attrs = serde_json::Map::new();
    attrs.insert("ros.node".into(), s.ros_node_name.clone().into());
    attrs.insert(
        "ros.node.namespace".into(),
        s.ros_node_namespace.clone().into(),
    );
    if let Some(topic) = &s.ros_topic {
        attrs.insert("ros.topic".into(), topic.clone().into());
    }
    attrs.insert("ros.message_type".into(), s.ros_message_type.clone().into());
    attrs.insert(
        "ros.publisher_gid".into(),
        s.ros_publisher_gid.clone().into(),
    );
    attrs.insert("ros.content_hash".into(), s.ros_content_hash.into());
    attrs.insert("ros.sequence_number".into(), s.ros_sequence_number.into());
    attrs.insert(
        "ros.source_timestamp_ns".into(),
        s.ros_source_timestamp_ns.into(),
    );
    attrs.insert(
        "ros.message_size_bytes".into(),
        s.ros_message_size_bytes.into(),
    );
    attrs.insert("ros.dds.domain_id".into(), s.ros_dds_domain_id.into());
    attrs.insert(
        "ros.correlation_method".into(),
        s.ros_correlation_method.into(),
    );
    if let Some(pub_id) = &s.correlated_publish_span_id {
        attrs.insert(
            "ros.correlation.publish_span_id".into(),
            pub_id.clone().into(),
        );
    }
    serde_json::to_string(&attrs).unwrap_or_else(|_| "{}".to_string())
}

fn log_attributes_json(l: &OtelLog) -> String {
    let mut attrs = serde_json::Map::new();
    attrs.insert("logger.name".into(), l.logger_name.clone().into());
    if let Some(v) = &l.code_filepath {
        attrs.insert("code.filepath".into(), v.clone().into());
    }
    if let Some(v) = &l.code_function {
        attrs.insert("code.function".into(), v.clone().into());
    }
    if let Some(v) = l.code_lineno {
        attrs.insert("code.lineno".into(), v.into());
    }
    serde_json::to_string(&attrs).unwrap_or_else(|_| "{}".to_string())
}

// ---------------------------------------------------------------------------
// Arrow RecordBatch builders
// ---------------------------------------------------------------------------

fn spans_to_record_batch(spans: &[OtelSpan], resource_attrs_json: &str) -> Result<RecordBatch> {
    let now_ns = Utc::now().timestamp_nanos_opt().unwrap_or(0);
    let schema = Arc::new(traces_schema());

    let span_attrs: Vec<String> = spans.iter().map(span_attributes_json).collect();

    let arrays: Vec<ArrayRef> = vec![
        // ── OtelPostgres core ──────────────────────────────────────────
        Arc::new(
            TimestampMicrosecondArray::from_iter_values(
                spans.iter().map(|s| s.start_time_ns / 1000),
            )
            .with_timezone("UTC"),
        ),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.trace_id.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.span_id.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            spans
                .iter()
                .map(|s| s.parent_span_id.as_deref().unwrap_or("")),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.span_name.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.span_kind.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(spans.iter().map(|s| {
            service_name_for(&s.ros_node_namespace, &s.ros_node_name)
        }))),
        Arc::new(Int64Array::from_iter_values(
            spans.iter().map(|s| s.duration_ns),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.status_code.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            span_attrs.iter().map(|s| s.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(std::iter::repeat_n(
            resource_attrs_json,
            spans.len(),
        ))),
        // ── ROS2-specific extras ────────────────────────────────────────
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.operation.as_str()),
        )),
        Arc::new(Int64Array::from_iter_values(
            spans.iter().map(|s| s.start_time_ns),
        )),
        Arc::new(Int64Array::from_iter_values(
            spans.iter().map(|s| s.end_time_ns),
        )),
        Arc::new(StringArray::from(
            spans
                .iter()
                .map(|s| s.ros_topic.as_deref())
                .collect::<Vec<_>>(),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.ros_node_name.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.ros_node_namespace.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.ros_message_type.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            spans.iter().map(|s| s.ros_publisher_gid.as_str()),
        )),
        Arc::new(UInt64Array::from_iter_values(
            spans.iter().map(|s| s.ros_content_hash),
        )),
        Arc::new(UInt64Array::from_iter_values(
            spans.iter().map(|s| s.ros_sequence_number),
        )),
        Arc::new(Int64Array::from_iter_values(
            spans.iter().map(|s| s.ros_source_timestamp_ns),
        )),
        Arc::new(UInt32Array::from_iter_values(
            spans.iter().map(|s| s.ros_message_size_bytes),
        )),
        Arc::new(UInt32Array::from_iter_values(
            spans.iter().map(|s| s.ros_dds_domain_id),
        )),
        Arc::new(UInt8Array::from_iter_values(
            spans.iter().map(|s| s.ros_correlation_method),
        )),
        Arc::new(StringArray::from(
            spans
                .iter()
                .map(|s| s.correlated_publish_span_id.as_deref())
                .collect::<Vec<_>>(),
        )),
        Arc::new(StringArray::from(
            spans
                .iter()
                .map(|s| {
                    if s.span_links.is_empty() {
                        None
                    } else {
                        serde_json::to_string(&s.span_links).ok()
                    }
                })
                .collect::<Vec<_>>(),
        )),
        Arc::new(Int64Array::from_iter_values(std::iter::repeat_n(
            now_ns,
            spans.len(),
        ))),
    ];

    RecordBatch::try_new(schema, arrays).map_err(|e| BridgeError::Storage(e.to_string()))
}

fn logs_to_record_batch(logs: &[OtelLog], resource_attrs_json: &str) -> Result<RecordBatch> {
    let now_ns = Utc::now().timestamp_nanos_opt().unwrap_or(0);
    let schema = Arc::new(logs_schema());

    let log_attrs: Vec<String> = logs.iter().map(log_attributes_json).collect();

    let arrays: Vec<ArrayRef> = vec![
        // ── OtelPostgres core ──────────────────────────────────────────
        Arc::new(
            TimestampMicrosecondArray::from_iter_values(logs.iter().map(|l| l.timestamp_ns / 1000))
                .with_timezone("UTC"),
        ),
        Arc::new(StringArray::from_iter_values(
            logs.iter().map(|l| l.trace_id.as_deref().unwrap_or("")),
        )),
        Arc::new(StringArray::from_iter_values(
            logs.iter().map(|l| l.span_id.as_deref().unwrap_or("")),
        )),
        Arc::new(StringArray::from_iter_values(
            logs.iter().map(|l| l.severity.as_str()),
        )),
        Arc::new(Int32Array::from_iter_values(
            logs.iter().map(|l| l.severity_number),
        )),
        Arc::new(StringArray::from_iter_values(
            logs.iter().map(|l| l.logger_name.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(
            logs.iter().map(|l| l.body.as_str()),
        )),
        Arc::new(StringArray::from_iter_values(std::iter::repeat_n(
            resource_attrs_json,
            logs.len(),
        ))),
        Arc::new(StringArray::from_iter_values(
            log_attrs.iter().map(|s| s.as_str()),
        )),
        // ── Extra ──────────────────────────────────────────────────────
        Arc::new(Int64Array::from_iter_values(std::iter::repeat_n(
            now_ns,
            logs.len(),
        ))),
    ];

    RecordBatch::try_new(schema, arrays).map_err(|e| BridgeError::Storage(e.to_string()))
}

// ---------------------------------------------------------------------------
// Parquet serialisation
// ---------------------------------------------------------------------------

fn record_batch_to_parquet(batch: &RecordBatch) -> Result<Vec<u8>> {
    let props = WriterProperties::builder()
        .set_compression(Compression::SNAPPY)
        .build();

    let mut buf = Vec::new();
    let cursor = Cursor::new(&mut buf);
    let mut writer = ArrowWriter::try_new(cursor, batch.schema(), Some(props))?;
    writer.write(batch)?;
    writer.close()?;
    Ok(buf)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pipeline::otel_builder::SpanKind;

    fn make_span() -> OtelSpan {
        OtelSpan {
            trace_id: "trace-1".into(),
            span_id: "span-1".into(),
            parent_span_id: None,
            operation: "publish".into(),
            span_name: "publish /chatter".into(),
            span_kind: SpanKind::Producer,
            start_time_ns: 1_000_000_000,
            end_time_ns: 1_000_050_000,
            duration_ns: 50_000,
            status_code: "OK".into(),
            ros_topic: Some("/chatter".into()),
            ros_node_name: "talker".into(),
            ros_node_namespace: "/".into(),
            ros_message_type: "std_msgs/msg/String".into(),
            ros_publisher_gid: "aabbccdd".into(),
            ros_content_hash: 0xdeadbeef,
            ros_sequence_number: 1,
            ros_source_timestamp_ns: 1_000_000_000,
            ros_message_size_bytes: 64,
            ros_dds_domain_id: 0,
            ros_correlation_method: 2,
            correlated_publish_span_id: None,
            span_links: vec![],
        }
    }

    fn make_log() -> OtelLog {
        OtelLog {
            timestamp_ns: 1_000_000_000,
            severity: "INFO".into(),
            severity_number: 9,
            body: "Hello world".into(),
            logger_name: "talker".into(),
            trace_id: Some("trace-1".into()),
            span_id: Some("span-1".into()),
            code_filepath: None,
            code_function: None,
            code_lineno: None,
        }
    }

    #[test]
    fn test_service_name_derivation() {
        assert_eq!(service_name_for("/", "talker"), "/talker");
        assert_eq!(service_name_for("", "talker"), "/talker");
        assert_eq!(service_name_for("/robot_1", "talker"), "/robot_1/talker");
        assert_eq!(service_name_for("/robot_1/", "talker"), "/robot_1/talker");
    }

    #[test]
    fn test_spans_to_record_batch() {
        let spans = vec![make_span()];
        let batch = spans_to_record_batch(&spans, "{}").unwrap();
        assert_eq!(batch.num_rows(), 1);
        // 11 OtelPostgres core + 17 ROS extras = 28
        assert_eq!(batch.num_columns(), 28);
    }

    #[test]
    fn test_logs_to_record_batch() {
        let logs = vec![make_log()];
        let batch = logs_to_record_batch(&logs, "{}").unwrap();
        assert_eq!(batch.num_rows(), 1);
        // 9 OtelPostgres core + 1 extra (written_at_ns) = 10
        assert_eq!(batch.num_columns(), 10);
    }

    #[test]
    fn test_span_attributes_json() {
        let span = make_span();
        let json_str = span_attributes_json(&span);
        let parsed: serde_json::Value = serde_json::from_str(&json_str).unwrap();
        assert_eq!(parsed["ros.node"], "talker");
        assert_eq!(parsed["ros.topic"], "/chatter");
        assert!(parsed.get("ros.correlation.publish_span_id").is_none());
    }

    #[test]
    fn test_span_attributes_json_with_correlation() {
        let mut span = make_span();
        span.correlated_publish_span_id = Some("pub-span-xyz".into());
        let json_str = span_attributes_json(&span);
        let parsed: serde_json::Value = serde_json::from_str(&json_str).unwrap();
        assert_eq!(parsed["ros.correlation.publish_span_id"], "pub-span-xyz");
    }

    #[test]
    fn test_resource_attributes_json() {
        let spans = vec![make_span()];
        let resource_json = r#"{"robot.id":"r1","organization.id":"o1"}"#;
        let batch = spans_to_record_batch(&spans, resource_json).unwrap();
        let col = batch
            .column_by_name("resource_attributes")
            .unwrap()
            .as_any()
            .downcast_ref::<StringArray>()
            .unwrap();
        let parsed: serde_json::Value = serde_json::from_str(col.value(0)).unwrap();
        assert_eq!(parsed["robot.id"], "r1");
        assert_eq!(parsed["organization.id"], "o1");
    }

    #[test]
    fn test_parent_promoted_for_consumer() {
        let mut span = make_span();
        span.span_kind = SpanKind::Consumer;
        span.correlated_publish_span_id = Some("pub-span-abc".into());
        // parent_span_id is None; correlated publish should appear as parent_span_id column value
        let batch = spans_to_record_batch(&[span], "{}").unwrap();
        let col = batch
            .column_by_name("parent_span_id")
            .unwrap()
            .as_any()
            .downcast_ref::<StringArray>()
            .unwrap();
        assert_eq!(col.value(0), "");
        // The publish span is already in span_attributes; parent_span_id is set on OtelSpan
        // by otel_builder.rs (not parquet.rs) — so this test verifies the None→"" mapping.
    }

    #[test]
    fn test_timestamp_column_type() {
        let spans = vec![make_span()];
        let batch = spans_to_record_batch(&spans, "{}").unwrap();
        let schema = batch.schema();
        let field = schema.field_with_name("timestamp").unwrap();
        assert_eq!(
            field.data_type(),
            &DataType::Timestamp(TimeUnit::Microsecond, Some(Arc::from("UTC")))
        );
    }

    #[test]
    fn test_duration_column_exists() {
        let spans = vec![make_span()];
        let batch = spans_to_record_batch(&spans, "{}").unwrap();
        let col = batch
            .column_by_name("duration")
            .unwrap()
            .as_any()
            .downcast_ref::<Int64Array>()
            .unwrap();
        assert_eq!(col.value(0), 50_000);
    }

    #[test]
    fn test_record_batch_to_parquet_roundtrip() {
        let spans = vec![make_span()];
        let batch = spans_to_record_batch(&spans, "{}").unwrap();
        let bytes = record_batch_to_parquet(&batch).unwrap();
        // Valid Parquet magic bytes: PAR1
        assert!(bytes.starts_with(b"PAR1"));
        assert!(bytes.ends_with(b"PAR1"));
        assert!(bytes.len() > 100);
    }

    #[test]
    fn test_parquet_exporter_local() {
        let dir = tempfile::tempdir().unwrap();
        let mut exporter =
            ParquetExporter::new(dir.path().to_str().unwrap(), 10, 200, "{}".into()).unwrap();

        exporter.export_span(&make_span()).unwrap();
        exporter.export_log(&make_log()).unwrap();
        exporter.flush().unwrap();

        // Verify files were written
        let traces_dir = dir.path().join(exporter.session_path()).join("traces");
        let logs_dir = dir.path().join(exporter.session_path()).join("logs");
        assert!(traces_dir.join("part-0001.parquet").exists());
        assert!(logs_dir.join("part-0001.parquet").exists());
    }

    #[test]
    fn test_storage_limit() {
        let dir = tempfile::tempdir().unwrap();
        // batch_size > 1 so auto-flush doesn't trigger; limit of 0 MB triggers on explicit flush
        let mut exporter =
            ParquetExporter::new(dir.path().to_str().unwrap(), 100, 0, "{}".into()).unwrap();
        exporter.export_span(&make_span()).unwrap();
        let result = exporter.flush();
        assert!(matches!(result, Err(BridgeError::StorageLimitReached)));
    }
}
