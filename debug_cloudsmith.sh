#!/bin/bash
# Debug script to diagnose Cloudsmith repository issue
set -e

echo "=== System Info ==="
uname -m
cat /etc/os-release | grep VERSION

echo ""
echo "=== Setting up Cloudsmith repository ==="
curl -u "$(cat ~/.cloudsmith/key)" -1sLf \
  'https://dl.cloudsmith.io/basic/robotops/robotops-development/setup.deb.sh' \
  | bash

echo ""
echo "=== Checking apt sources ==="
ls -la /etc/apt/sources.list.d/

echo ""
echo "=== Content of Cloudsmith source file ==="
cat /etc/apt/sources.list.d/robotops-robotops-development.list 2>/dev/null || echo "File not found"

echo ""
echo "=== Updating apt cache ==="
apt-get update

echo ""
echo "=== Searching for robotops packages ==="
apt-cache search robotops || echo "No packages found"

echo ""
echo "=== Trying to show package info ==="
apt-cache show ros-jazzy-robotops-msgs 2>&1 || echo "Package not in cache"

echo ""
echo "=== Checking available packages in Cloudsmith repo ==="
apt-cache policy ros-jazzy-robotops-msgs 2>&1 || echo "No policy info"

echo ""
echo "=== Listing all ros-jazzy packages ==="
apt-cache search ros-jazzy | head -20
