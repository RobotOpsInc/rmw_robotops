# RMW RobotOps Examples

This directory contains examples and validation scripts for the rmw_robotops distributed tracing system.

## End-to-End Validation

The `validate_tracing.sh` script performs a complete end-to-end test of the tracing system:

### What it does

1. **Creates simple ROS2 nodes:**
   - Publisher node that sends messages to `/validation_topic`
   - Subscriber node that receives messages from `/validation_topic`
   - Trace monitor that listens to `/robotops/trace_events`

2. **Verifies trace events:**
   - Confirms trace events are emitted for publish/subscribe operations
   - Validates metadata fields are populated (node_name, namespace, message_type)
   - Checks trace IDs and span IDs are present

3. **Reports results:**
   - Prints summary of trace events received
   - Shows sample event with full metadata
   - Returns exit code 0 on success, 1 on failure

### Prerequisites

- ROS2 Jazzy installed and sourced
- rmw_robotops built and installed
- robotops_msgs package available
- Python 3 with rclpy

### Usage

```bash
# From the Docker container
cd /workspace/src/rmw_robotops/examples
./validate_tracing.sh
```

### Expected Output

```
=== RMW RobotOps End-to-End Validation ===

Environment Configuration:
  RMW_IMPLEMENTATION: rmw_robotops
  ROBOTOPS_UNDERLYING_RMW: rmw_fastrtps_cpp
  ROBOTOPS_TRACING_ENABLED: 1

Starting validation test (will run for ~15 seconds)...

1. Starting trace event monitor...
2. Starting subscriber...
3. Starting publisher...
[INFO] Published: "Validation message 0"
[INFO] Received [1]: "Validation message 0"
[INFO] Trace [1] PUBLISH: topic=/validation_topic, node=validation_publisher, type=std_msgs/msg/String
[INFO] Trace [2] SUBSCRIBE: topic=/validation_topic, node=validation_subscriber, type=std_msgs/msg/String
...

============================================================
TRACE EVENT SUMMARY
============================================================
Total trace events: 20
  - PUBLISH events: 10
  - SUBSCRIBE events: 10

Sample PUBLISH event metadata:
  - trace_id: 1a2b3c4d5e6f7890abcdef1234567890
  - span_id: 1234567890abcdef
  - node_name: validation_publisher
  - node_namespace: /
  - topic: /validation_topic
  - message_type: std_msgs/msg/String

✅ VALIDATION PASSED: Trace events contain metadata!

=== ✅ END-TO-END VALIDATION SUCCESSFUL ===
```

### Troubleshooting

**No trace events received:**
- Check that `ROBOTOPS_TRACING_ENABLED=1` is set
- Verify rmw_robotops is being loaded (check stderr for "Loaded underlying RMW" message)
- Ensure robotops_msgs is installed

**Metadata fields empty:**
- This indicates a bug in the metadata extraction
- Check that publisher/subscriber creation is properly intercepted
- Verify type support is available for std_msgs

**Trace monitor not receiving events:**
- Check that `/robotops/trace_events` topic exists: `ros2 topic list`
- Verify background publisher thread started (check stderr for warnings)
- Check DDS discovery is working: `ros2 node list`

## Manual Testing

You can also manually test the system by running ROS2 nodes and monitoring trace events:

```bash
# Terminal 1: Set environment
export RMW_IMPLEMENTATION=rmw_robotops
export ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp
export ROBOTOPS_TRACING_ENABLED=1

# Monitor trace events
ros2 topic echo /robotops/trace_events robotops_msgs/msg/TraceEvent

# Terminal 2: Run any ROS2 publisher
ros2 topic pub /test std_msgs/msg/String "data: 'Hello tracing'"

# Terminal 3: Run any ROS2 subscriber
ros2 topic echo /test
```

You should see trace events appearing in Terminal 1 with metadata about the publisher and subscriber nodes.
