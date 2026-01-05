# syntax=docker/dockerfile:1.4

# ============================================================================
# Base stage - Common dependencies for all targets
# ============================================================================
FROM ros:jazzy-ros-base AS base

# Build arguments for configurable Cloudsmith repository and package version
ARG CLOUDSMITH_REPO=robotops-development
ARG ROBOTOPS_MSGS_VERSION=0.1.6-0noble

# Install core build dependencies and packaging tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3-colcon-common-extensions \
    python3-rosdep \
    python3-bloom \
    fakeroot \
    dpkg-dev \
    debhelper \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install FastDDS (our primary DDS implementation)
RUN apt-get update && apt-get install -y \
    ros-jazzy-rmw-fastrtps-cpp \
    ros-jazzy-fastrtps \
    && rm -rf /var/lib/apt/lists/*

# Configure Cloudsmith repository using buildx secret
# Secret file format: username:api_key
RUN --mount=type=secret,id=cloudsmith_key \
    CLOUDSMITH_CREDS=$(cat /run/secrets/cloudsmith_key) && \
    curl -u "${CLOUDSMITH_CREDS}" -1sLf \
    "https://dl.cloudsmith.io/basic/robotops/${CLOUDSMITH_REPO}/setup.deb.sh" \
    | bash

# Install robotops_msgs from Cloudsmith
RUN apt-get update && apt-get install -y \
    ros-jazzy-robotops-msgs=${ROBOTOPS_MSGS_VERSION} \
    && rm -rf /var/lib/apt/lists/*

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
