//! Error types for robotops-demo-agent.

use thiserror::Error;

#[derive(Error, Debug)]
pub enum BridgeError {
    #[error("ROS2 error: {0}")]
    Ros2(String),

    #[error("Invalid output path '{0}': {1}")]
    InvalidOutput(String, String),

    #[error("Storage error: {0}")]
    Storage(String),

    #[error("Storage limit reached")]
    StorageLimitReached,

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

impl From<object_store::Error> for BridgeError {
    fn from(e: object_store::Error) -> Self {
        BridgeError::Storage(e.to_string())
    }
}

impl From<parquet::errors::ParquetError> for BridgeError {
    fn from(e: parquet::errors::ParquetError) -> Self {
        BridgeError::Storage(e.to_string())
    }
}

pub type Result<T> = std::result::Result<T, BridgeError>;
