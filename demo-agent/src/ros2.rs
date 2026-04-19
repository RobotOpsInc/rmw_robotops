//! ROS2 context and node lifecycle management.
//!
//! Provides a single shared r2r node for all demo-agent subscriptions, with a
//! dedicated spin thread to deliver DDS messages to the subscription streams.
//!
//! Adapted from robot_agent's `ros2_context.rs` — simplified to a single node
//! rather than multiple dedicated nodes per subsystem.

use crate::error::{BridgeError, Result};
use parking_lot::Mutex;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};

const BRIDGE_NODE_NAME: &str = "robotops_demo_agent";

/// Shared ROS2 context wrapping an r2r node and its spin thread.
pub struct Ros2Context {
    /// Shared r2r node (all subscriptions attach to this node).
    node: Arc<Mutex<r2r::Node>>,
    /// Signals the spin thread to stop.
    spin_running: Arc<AtomicBool>,
    /// Spin thread handle.
    spin_thread: Option<std::thread::JoinHandle<()>>,
}

impl Ros2Context {
    /// Create a new ROS2 context with a single shared node and spin thread.
    pub fn new() -> Result<Self> {
        let context = r2r::Context::create()
            .map_err(|e| BridgeError::Ros2(format!("Failed to create r2r context: {}", e)))?;

        let node = r2r::Node::create(context, BRIDGE_NODE_NAME, "").map_err(|e| {
            BridgeError::Ros2(format!(
                "Failed to create r2r node '{}': {}",
                BRIDGE_NODE_NAME, e
            ))
        })?;

        let node = Arc::new(Mutex::new(node));

        let spin_running = Arc::new(AtomicBool::new(true));
        let spin_node = node.clone();
        let spin_flag = spin_running.clone();

        let spin_thread = std::thread::Builder::new()
            .name("demo-agent-spin".to_string())
            .spawn(move || {
                while spin_flag.load(Ordering::Relaxed) {
                    spin_node
                        .lock()
                        .spin_once(std::time::Duration::from_millis(10));
                }
                tracing::debug!("r2r spin thread exiting");
            })
            .map_err(|e| BridgeError::Ros2(format!("Failed to spawn spin thread: {}", e)))?;

        Ok(Self {
            node,
            spin_running,
            spin_thread: Some(spin_thread),
        })
    }

    /// Returns a clone of the shared node handle.
    pub fn node(&self) -> Arc<Mutex<r2r::Node>> {
        self.node.clone()
    }
}

impl Drop for Ros2Context {
    fn drop(&mut self) {
        self.spin_running.store(false, Ordering::Relaxed);
        if let Some(thread) = self.spin_thread.take() {
            let _ = thread.join();
        }
    }
}
