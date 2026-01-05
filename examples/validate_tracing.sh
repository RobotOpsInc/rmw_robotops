#!/bin/bash
# End-to-end validation script for rmw_robotops tracing system
# This script verifies that trace events are correctly emitted and contain metadata

set -e

echo "=== RMW RobotOps End-to-End Validation ==="
echo ""

# Source the workspace (assumes running from workspace)
if [ -f "/workspace/install/setup.bash" ]; then
    echo "Sourcing workspace..."
    source /workspace/install/setup.bash
elif [ -f "../../../install/setup.bash" ]; then
    source ../../../install/setup.bash
fi

# Set environment to use rmw_robotops
export RMW_IMPLEMENTATION=rmw_robotops
export ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp
export ROBOTOPS_TRACING_ENABLED=1

# Check environment
echo "Environment Configuration:"
echo "  RMW_IMPLEMENTATION: $RMW_IMPLEMENTATION"
echo "  ROBOTOPS_UNDERLYING_RMW: $ROBOTOPS_UNDERLYING_RMW"
echo "  ROBOTOPS_TRACING_ENABLED: $ROBOTOPS_TRACING_ENABLED"
echo ""

# Create a temporary workspace for validation
VALIDATION_DIR="/tmp/rmw_robotops_validation_$$"
mkdir -p "$VALIDATION_DIR"
cd "$VALIDATION_DIR"

echo "Creating validation Python scripts..."

# Create a simple publisher
cat > publisher.py << 'EOF'
#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from std_msgs.msg import String
import sys

class SimplePublisher(Node):
    def __init__(self):
        super().__init__('validation_publisher')
        self.publisher = self.create_publisher(String, '/validation_topic', 10)
        self.timer = self.create_timer(0.5, self.publish_message)
        self.count = 0
        self.max_messages = 10

    def publish_message(self):
        if self.count >= self.max_messages:
            self.get_logger().info(f'Published {self.count} messages, shutting down')
            rclpy.shutdown()
            return

        msg = String()
        msg.data = f'Validation message {self.count}'
        self.publisher.publish(msg)
        self.get_logger().info(f'Published: "{msg.data}"')
        self.count += 1

def main():
    rclpy.init()
    node = SimplePublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
EOF

# Create a simple subscriber
cat > subscriber.py << 'EOF'
#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from std_msgs.msg import String

class SimpleSubscriber(Node):
    def __init__(self):
        super().__init__('validation_subscriber')
        self.subscription = self.create_subscription(
            String,
            '/validation_topic',
            self.message_callback,
            10)
        self.message_count = 0

    def message_callback(self, msg):
        self.message_count += 1
        self.get_logger().info(f'Received [{self.message_count}]: "{msg.data}"')

def main():
    rclpy.init()
    node = SimpleSubscriber()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
EOF

# Create trace event monitor
cat > trace_monitor.py << 'EOF'
#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from robotops_msgs.msg import TraceEvent
import sys

class TraceMonitor(Node):
    def __init__(self):
        super().__init__('trace_monitor')
        self.subscription = self.create_subscription(
            TraceEvent,
            '/robotops/trace_events',
            self.trace_callback,
            100)  # Large queue for trace events
        self.trace_count = 0
        self.publish_events = []
        self.subscribe_events = []

    def trace_callback(self, msg):
        self.trace_count += 1

        # Operation types: 1=PUBLISH, 2=SUBSCRIBE
        op_name = {1: 'PUBLISH', 2: 'SUBSCRIBE'}.get(msg.operation, 'UNKNOWN')

        self.get_logger().info(
            f'Trace [{self.trace_count}] {op_name}: '
            f'topic={msg.topic_or_service}, '
            f'node={msg.node_name}, '
            f'type={msg.message_type}'
        )

        # Collect events for validation
        if msg.operation == 1:  # PUBLISH
            self.publish_events.append(msg)
        elif msg.operation == 2:  # SUBSCRIBE
            self.subscribe_events.append(msg)

    def print_summary(self):
        print('\n' + '='*60)
        print('TRACE EVENT SUMMARY')
        print('='*60)
        print(f'Total trace events: {self.trace_count}')
        print(f'  - PUBLISH events: {len(self.publish_events)}')
        print(f'  - SUBSCRIBE events: {len(self.subscribe_events)}')
        print()

        # Validate metadata
        validation_passed = True

        if len(self.publish_events) > 0:
            print('Sample PUBLISH event metadata:')
            event = self.publish_events[0]
            print(f'  - trace_id: {event.trace_id}')
            print(f'  - span_id: {event.span_id}')
            print(f'  - node_name: {event.node_name}')
            print(f'  - node_namespace: {event.node_namespace}')
            print(f'  - topic: {event.topic_or_service}')
            print(f'  - message_type: {event.message_type}')

            # Validate required fields are populated
            if not event.node_name:
                print('  ❌ ERROR: node_name is empty!')
                validation_passed = False
            if not event.message_type:
                print('  ❌ ERROR: message_type is empty!')
                validation_passed = False

        print()
        if validation_passed and self.trace_count > 0:
            print('✅ VALIDATION PASSED: Trace events contain metadata!')
        elif self.trace_count == 0:
            print('❌ VALIDATION FAILED: No trace events received!')
            sys.exit(1)
        else:
            print('❌ VALIDATION FAILED: Metadata fields missing!')
            sys.exit(1)

def main():
    rclpy.init()
    node = TraceMonitor()

    # Run for 15 seconds to collect trace events
    import time
    import threading

    def shutdown_timer():
        time.sleep(15)
        node.get_logger().info('Shutting down trace monitor...')
        rclpy.shutdown()

    timer_thread = threading.Thread(target=shutdown_timer, daemon=True)
    timer_thread.start()

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.print_summary()
        node.destroy_node()

if __name__ == '__main__':
    main()
EOF

chmod +x publisher.py subscriber.py trace_monitor.py

echo "Starting validation test (will run for ~15 seconds)..."
echo ""

# Start trace monitor in background
echo "1. Starting trace event monitor..."
python3 trace_monitor.py &
MONITOR_PID=$!

# Give monitor time to start
sleep 2

# Start subscriber in background
echo "2. Starting subscriber..."
python3 subscriber.py &
SUB_PID=$!

# Give subscriber time to start
sleep 1

# Start publisher (runs for a few seconds then exits)
echo "3. Starting publisher..."
python3 publisher.py &
PUB_PID=$!

# Wait for publisher to finish
wait $PUB_PID 2>/dev/null || true

echo ""
echo "4. Waiting for trace monitor to finish..."

# Wait for monitor to finish (has 15s timeout)
wait $MONITOR_PID 2>/dev/null
MONITOR_EXIT=$?

# Clean up subscriber
kill $SUB_PID 2>/dev/null || true
wait $SUB_PID 2>/dev/null || true

# Check result
echo ""
if [ $MONITOR_EXIT -eq 0 ]; then
    echo "=== ✅ END-TO-END VALIDATION SUCCESSFUL ==="
    rm -rf "$VALIDATION_DIR"
    exit 0
else
    echo "=== ❌ END-TO-END VALIDATION FAILED ==="
    echo "Check logs above for details"
    rm -rf "$VALIDATION_DIR"
    exit 1
fi
