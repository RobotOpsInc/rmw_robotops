# syntax=docker/dockerfile:1.4
# Build environment for robotops-demo-agent
# Based on ROS2 Jazzy with Rust stable toolchain
FROM ros:jazzy

# System dependencies
# libclang-dev + clang are required by r2r's bindgen step
RUN apt-get update && apt-get install -y \
    curl \
    build-essential \
    pkg-config \
    libclang-dev \
    clang \
    ca-certificates \
    gpg \
    && rm -rf /var/lib/apt/lists/*

# Configure TraceHouse APT repository and install robotops-msgs
# (r2r needs the message type support libraries at build and runtime)
RUN curl -fsSL https://apt.robotops.com/robotops-public-key.asc \
        | gpg --dearmor -o /usr/share/keyrings/robotops-archive-keyring.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/robotops-archive-keyring.gpg] https://apt.robotops.com noble main" \
        > /etc/apt/sources.list.d/robotops.list && \
    apt-get update && \
    apt-get install -y ros-jazzy-robotops-msgs && \
    rm -rf /var/lib/apt/lists/*

# Install Rust stable via rustup
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --default-toolchain stable
ENV PATH="/root/.cargo/bin:${PATH}"

# Add rustfmt and clippy
RUN rustup component add rustfmt clippy

WORKDIR /workspace

RUN echo "source /opt/ros/jazzy/setup.bash" >> /root/.bashrc
CMD ["/bin/bash", "-c", "source /opt/ros/jazzy/setup.bash && exec bash"]
