#!/usr/bin/env python3
"""Scripted turtlesim scenario for e2e tracing verification.

Exercises:
  - PRODUCER spans: publishing /turtle1/cmd_vel, /turtle2/cmd_vel
  - CONSUMER spans: subscribing to /turtle1/pose (cross-process correlation)
  - SERVER spans:   /spawn, /kill, /turtle1/set_pen service calls
  - Action spans:   /turtle1/rotate_absolute (underlying pub/sub + service traffic)
  - Log correlation: logger.info() from pose callback so TraceContextRegistry fires
"""

import math
import sys
import time

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node

from geometry_msgs.msg import Twist
from turtlesim.action import RotateAbsolute
from turtlesim.msg import Pose
from turtlesim.srv import Kill, SetPen, Spawn


DRIVE_SECONDS = 20.0
LINEAR_SPEED = 1.5
ANGULAR_SPEED = 1.5


class TurtleDriver(Node):
    def __init__(self):
        super().__init__("turtle_driver")

        self._vel1 = self.create_publisher(Twist, "/turtle1/cmd_vel", 10)
        self._vel2_pub = None  # created after /spawn

        self._pose_count = 0
        self._pose_sub = self.create_subscription(
            Pose, "/turtle1/pose", self._on_pose, 10
        )

        self._spawn = self.create_client(Spawn, "/spawn")
        self._kill = self.create_client(Kill, "/kill")
        self._set_pen = self.create_client(SetPen, "/turtle1/set_pen")
        self._rotate = ActionClient(self, RotateAbsolute, "/turtle1/rotate_absolute")

    # ── callbacks ────────────────────────────────────────────────────────────

    def _on_pose(self, msg: Pose):
        # Emit a logger call while inside a subscribe callback so the active
        # trace context (set by rmw_robotops) is captured in TraceContextRegistry
        # and stamped onto the resulting /rosout log record.
        self._pose_count += 1
        if self._pose_count % 20 == 0:
            self.get_logger().info(
                f"pose: x={msg.x:.2f} y={msg.y:.2f} θ={msg.theta:.2f} "
                f"[sample #{self._pose_count}]"
            )

    # ── helpers ───────────────────────────────────────────────────────────────

    def _wait_for_service(self, client, timeout=10.0):
        if not client.wait_for_service(timeout_sec=timeout):
            self.get_logger().error(f"Service {client.srv_name} not available")
            sys.exit(1)

    def _call_service(self, client, request):
        future = client.call_async(request)
        rclpy.spin_until_future_complete(self, future, timeout_sec=10.0)
        return future.result()

    def _publish_square(self, side_duration=2.0, turn_duration=0.75):
        """Drive a square: four sides + four turns."""
        self.get_logger().info("Driving square path on /turtle1/cmd_vel...")
        for _ in range(4):
            # Straight
            deadline = time.monotonic() + side_duration
            while time.monotonic() < deadline:
                t = Twist()
                t.linear.x = LINEAR_SPEED
                self._vel1.publish(t)
                rclpy.spin_once(self, timeout_sec=0.0)
                time.sleep(0.05)
            # Turn
            deadline = time.monotonic() + turn_duration
            while time.monotonic() < deadline:
                t = Twist()
                t.angular.z = ANGULAR_SPEED
                self._vel1.publish(t)
                rclpy.spin_once(self, timeout_sec=0.0)
                time.sleep(0.05)

    def _publish_circle(self, duration=4.0):
        """Drive a circle on /turtle2/cmd_vel (if turtle2 exists)."""
        if self._vel2_pub is None:
            return
        self.get_logger().info("Driving circle path on /turtle2/cmd_vel...")
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            t = Twist()
            t.linear.x = 1.0
            t.angular.z = 1.0
            self._vel2_pub.publish(t)
            rclpy.spin_once(self, timeout_sec=0.0)
            time.sleep(0.05)

    def _do_rotate_absolute(self, theta: float):
        """Send a RotateAbsolute action goal and wait for result."""
        self.get_logger().info(f"Sending rotate_absolute goal: theta={theta:.2f}")
        if not self._rotate.wait_for_server(timeout_sec=10.0):
            self.get_logger().warn("rotate_absolute action server not available, skipping")
            return
        goal = RotateAbsolute.Goal()
        goal.theta = theta
        future = self._rotate.send_goal_async(goal)
        rclpy.spin_until_future_complete(self, future, timeout_sec=10.0)
        goal_handle = future.result()
        if goal_handle is None or not goal_handle.accepted:
            self.get_logger().warn("rotate_absolute goal rejected")
            return
        result_future = goal_handle.get_result_async()
        rclpy.spin_until_future_complete(self, result_future, timeout_sec=15.0)
        self.get_logger().info("rotate_absolute completed")

    # ── main scenario ────────────────────────────────────────────────────────

    def run_scenario(self):
        self.get_logger().info("=== turtle_driver: starting scenario ===")

        # 1. Wait for services
        for client in [self._spawn, self._kill, self._set_pen]:
            self._wait_for_service(client)

        # 2. Set pen color (SERVER span)
        self.get_logger().info("Calling /turtle1/set_pen (SERVICE span)...")
        self._call_service(self._set_pen, SetPen.Request(r=200, g=50, b=50, width=3, off=0))

        # 3. Spawn turtle2 (SERVER span)
        self.get_logger().info("Calling /spawn for turtle2 (SERVICE span)...")
        self._call_service(
            self._spawn,
            Spawn.Request(x=3.0, y=5.0, theta=0.0, name="turtle2"),
        )
        self._vel2_pub = self.create_publisher(Twist, "/turtle2/cmd_vel", 10)

        # 4. Drive a square on turtle1 (PRODUCER spans)
        self._publish_square()

        # 5. Rotate absolute action on turtle1 (action traffic)
        self._do_rotate_absolute(math.pi / 2)

        # 6. Drive circle on turtle2 (PRODUCER spans on /turtle2/cmd_vel)
        self._publish_circle()

        # 7. Kill turtle2 (SERVER span)
        self.get_logger().info("Calling /kill for turtle2 (SERVICE span)...")
        self._call_service(self._kill, Kill.Request(name="turtle2"))

        self.get_logger().info(
            f"=== scenario complete — captured {self._pose_count} pose samples ==="
        )


def main():
    rclpy.init()
    node = TurtleDriver()

    # Let subscriptions settle before driving
    for _ in range(20):
        rclpy.spin_once(node, timeout_sec=0.1)

    node.run_scenario()

    # Keep spinning briefly so final pose callbacks and /rosout messages flush
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)

    node.destroy_node()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
