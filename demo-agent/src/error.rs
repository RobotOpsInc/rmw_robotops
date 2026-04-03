//! Error types for robotops-demo-agent.

use thiserror::Error;

#[derive(Error, Debug)]
pub enum BridgeError {
    #[error("ROS2 error: {0}")]
    Ros2(String),

    #[error("Invalid output URI '{0}': {1}")]
    InvalidUri(String, String),

    #[error("Feature not enabled: install with --features {0}")]
    FeatureNotEnabled(String),

    #[error("Database error: {0}")]
    Database(String),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

#[cfg(feature = "duckdb-output")]
impl From<duckdb::Error> for BridgeError {
    fn from(e: duckdb::Error) -> Self {
        BridgeError::Database(e.to_string())
    }
}

pub type Result<T> = std::result::Result<T, BridgeError>;
