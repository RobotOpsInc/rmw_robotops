# syntax=docker/dockerfile:1.4

# ============================================================================
# Base stage - Common dependencies for all targets
# ============================================================================
FROM ros:jazzy-ros-base AS base

# Build arguments for configurable Cloudsmith repository
# Production
ARG CLOUDSMITH_REPO=robotops
# Development
# ARG CLOUDSMITH_REPO=robotops-development
ARG CLOUDSMITH_USERNAME
ARG CLOUDSMITH_API_KEY

# Install core build dependencies and packaging tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3-colcon-common-extensions \
    python3-rosdep \
    python3-bloom \
    python3-pip \
    fakeroot \
    dpkg-dev \
    debhelper \
    curl \
    ca-certificates \
    libprotobuf-dev \
    protobuf-compiler \
    libxxhash-dev \
    && rm -rf /var/lib/apt/lists/*

# Install just command runner (make installation non-fatal in case of download issues)
RUN curl -fsSL https://just.systems/install.sh | bash -s -- --to /usr/local/bin || echo "Warning: just installation failed, but continuing..."

# Configure Cloudsmith APT repositories
# 1. Development repository (private) - for in-development packages
# 2. Public repository - for released packages like robotops-config
RUN echo "deb [trusted=yes] https://${CLOUDSMITH_USERNAME}:${CLOUDSMITH_API_KEY}@dl.cloudsmith.io/basic/robotops/${CLOUDSMITH_REPO}/deb/ubuntu noble main" \
    > /etc/apt/sources.list.d/robotops-${CLOUDSMITH_REPO}.list && \
    curl -1sLf 'https://dl.cloudsmith.io/public/robotops/robotops/setup.deb.sh' | bash && \
    # Update package cache to include new repositories
    apt-get update

# Add custom rosdep rules for RobotOps packages (from Cloudsmith, not rosdistro)
RUN mkdir -p /etc/ros/rosdep/sources.list.d && \
    printf '%s\n' \
    'robotops-config:' '  ubuntu:' '    - ros-jazzy-robotops-config' \
    'robotops_msgs:' '  ubuntu:' '    - ros-jazzy-robotops-msgs' \
    > /etc/ros/rosdep/custom.yaml && \
    echo 'yaml file:///etc/ros/rosdep/custom.yaml' > /etc/ros/rosdep/sources.list.d/50-custom.list

# Copy package.xml to install dependencies from it (single source of truth)
WORKDIR /workspace/src/rmw_robotops
COPY package.xml .

# Initialize rosdep and install dependencies from package.xml
# This respects version constraints like version_gte="0.2.1" in package.xml
RUN rosdep update && \
    rosdep install --from-paths . --ignore-src -y --rosdistro jazzy

WORKDIR /workspace

# ============================================================================
# Development stage - For interactive development
# ============================================================================
FROM base AS dev

# Source ROS in bashrc for interactive use
RUN echo "source /opt/ros/jazzy/setup.bash" >> ~/.bashrc

CMD ["/bin/bash"]

# ============================================================================
# Test stage - Adds sanitizers for safety testing
# ============================================================================
FROM base AS test

# Install sanitizer libraries
RUN apt-get update && apt-get install -y \
    clang \
    llvm \
    libasan8 \
    libubsan1 \
    && rm -rf /var/lib/apt/lists/*

CMD ["/bin/bash"]
