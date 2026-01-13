#!/bin/bash
# Simple validation - just check if rmw_robotops loads and initializes

set -e

echo "=== Simple RMW RobotOps Validation ==="
echo ""

# Source workspace
if [ -f "/workspace/install/setup.bash" ]; then
    source /workspace/install/setup.bash
fi

# Set environment
export RMW_IMPLEMENTATION=rmw_robotops
export ROBOTOPS_UNDERLYING_RMW=rmw_fastrtps_cpp
export ROBOTOPS_TRACING_ENABLED=1

echo "Environment:"
echo "  RMW_IMPLEMENTATION=$RMW_IMPLEMENTATION"
echo "  ROBOTOPS_UNDERLYING_RMW=$ROBOTOPS_UNDERLYING_RMW"
echo ""

# Test 1: Check if library exists
echo "Test 1: Check library exists"
if [ -f "/workspace/install/rmw_robotops/lib/librmw_robotops.so" ]; then
    echo "✅ librmw_robotops.so found"
else
    echo "❌ librmw_robotops.so NOT found"
    exit 1
fi
echo ""

# Test 2: Try to load RMW and list topics (this will initialize rmw_robotops)
echo "Test 2: Initialize RMW and list topics"
if timeout 5 ros2 topic list >/dev/null 2>&1; then
    echo "✅ RMW initialized successfully"
else
    echo "❌ RMW initialization failed or timed out"
    exit 1
fi
echo ""

# Test 3: Check if trace topic exists
echo "Test 3: Check for /robotops/trace_events topic"
if ros2 topic list 2>/dev/null | grep -q "/robotops/trace_events"; then
    echo "✅ Trace topic exists: /robotops/trace_events"
else
    echo "⚠️  Trace topic not found (may appear after first publish)"
fi
echo ""

# Test 4: Check topic type
echo "Test 4: Verify trace topic type"
TOPIC_TYPE=$(ros2 topic info /robotops/trace_events 2>/dev/null | grep "Type:" | awk '{print $2}' || echo "")
if [ "$TOPIC_TYPE" = "robotops_msgs/msg/TraceEvent" ]; then
    echo "✅ Trace topic has correct type: $TOPIC_TYPE"
elif [ -z "$TOPIC_TYPE" ]; then
    echo "ℹ️  Trace topic not yet published (normal until first message)"
else
    echo "❌ Trace topic has wrong type: $TOPIC_TYPE"
    exit 1
fi
echo ""

echo "=== ✅ BASIC VALIDATION PASSED ==="
echo ""
echo "Next steps:"
echo "  - Run: ros2 topic pub /test std_msgs/msg/String 'data: test'"
echo "  - Monitor: ros2 topic echo /robotops/trace_events"
echo "  - Verify trace events appear with metadata"
