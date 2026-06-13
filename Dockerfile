# syntax=docker/dockerfile:1.4

# ============================================================================
# Base stage - Common dependencies for all targets
# ============================================================================
#
# Parameterized by ROS distro so a single Dockerfile builds for either
# Jazzy (Ubuntu 24.04 Noble) or Humble (Ubuntu 22.04 Jammy, the arm64/Jetson
# target). Pass --build-arg ROS_DISTRO=humble for the Humble build.
#
#   ROS_DISTRO  ROS 2 distribution (jazzy | humble). Selects the
#               `ros:${ROS_DISTRO}-ros-base` base image and /opt/ros/${ROS_DISTRO},
#               and the ros-${ROS_DISTRO}-robotops-* deps pulled from apt.
ARG ROS_DISTRO=jazzy
FROM ros:${ROS_DISTRO}-ros-base AS base

# Re-declare after FROM so the value is in scope in the build stage.
ARG ROS_DISTRO

# Build argument for configurable APT repository environment
# Production: apt.robotops.com
# Development: apt.development.robotops.com
ARG APT_REPO_URL=https://apt.robotops.com

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
    gnupg \
    && rm -rf /var/lib/apt/lists/*

# Install just command runner (make installation non-fatal in case of download issues)
RUN curl -fsSL https://just.systems/install.sh | bash -s -- --to /usr/local/bin || echo "Warning: just installation failed, but continuing..."

# Configure RobotOps APT repository.
# The shared `robotops` aptly repo publishes the same package set to every
# Ubuntu codename (noble/jammy/focal), so pull from the channel matching this
# image's distro: jazzy → noble, humble → jammy. $UBUNTU_CODENAME is exported by
# /etc/os-release in the ros:${ROS_DISTRO}-ros-base base image.
RUN . /etc/os-release && \
    curl -fsSL ${APT_REPO_URL}/robotops-public-key.asc | gpg --dearmor -o /usr/share/keyrings/robotops-archive-keyring.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/robotops-archive-keyring.gpg] ${APT_REPO_URL} ${UBUNTU_CODENAME} main" \
    > /etc/apt/sources.list.d/robotops.list && \
    apt-get update

# Add custom rosdep rules for RobotOps packages.
# The robotops_msgs / robotops-config package.xml deps resolve to the
# ros-${ROS_DISTRO}-* debs published to apt.robotops.com (ros-jazzy-* / ros-humble-*).
RUN mkdir -p /etc/ros/rosdep/sources.list.d && \
    printf '%s\n' \
    'robotops-config:' '  ubuntu:' "    - ros-${ROS_DISTRO}-robotops-config" \
    'robotops_msgs:' '  ubuntu:' "    - ros-${ROS_DISTRO}-robotops-msgs" \
    > /etc/ros/rosdep/custom.yaml && \
    echo 'yaml file:///etc/ros/rosdep/custom.yaml' > /etc/ros/rosdep/sources.list.d/50-custom.list

# Copy package.xml to install dependencies from it (single source of truth)
WORKDIR /workspace/src/rmw_robotops
COPY package.xml .

# Initialize rosdep and install dependencies from package.xml
# This respects version constraints like version_gte="0.2.1" in package.xml
RUN rosdep update && \
    rosdep install --from-paths . --ignore-src -y --rosdistro ${ROS_DISTRO}

WORKDIR /workspace

# ============================================================================
# Development stage - For interactive development
# ============================================================================
FROM base AS dev

# Re-declare after FROM so ROS_DISTRO is in scope in this stage.
ARG ROS_DISTRO

# Source ROS in bashrc for interactive use
RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash" >> ~/.bashrc

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
