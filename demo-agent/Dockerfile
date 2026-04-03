# syntax=docker/dockerfile:1.4
# Build environment for robotops-demo-agent
# Based on ROS2 Jazzy with Rust stable toolchain
FROM ros:jazzy

# System dependencies
# libclang-dev + clang are required by r2r's bindgen step
# libduckdb-dev is installed from DuckDB's apt repo so we avoid compiling
# the ~500k-line DuckDB amalgamation from source on every CI run.
RUN apt-get update && apt-get install -y \
    curl \
    build-essential \
    pkg-config \
    libclang-dev \
    clang \
    ca-certificates \
    gpg \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Install DuckDB system library so the Rust crate links against it rather
# than rebuilding from source (no "bundled" feature needed in CI).
# Version must match the libduckdb-sys crate version (v1.MMPP0.0 → DuckDB vM.MM.PP).
# libduckdb-sys v1.10501.0 → DuckDB v1.5.1
RUN DUCKDB_VER=v1.5.1 && \
    ARCH=$(uname -m | sed 's/aarch64/arm64/;s/x86_64/amd64/') && \
    curl -fsSL "https://github.com/duckdb/duckdb/releases/download/${DUCKDB_VER}/libduckdb-linux-${ARCH}.zip" \
        -o /tmp/libduckdb.zip && \
    unzip -o /tmp/libduckdb.zip -d /tmp/libduckdb && \
    cp /tmp/libduckdb/libduckdb.so /usr/local/lib/ && \
    cp /tmp/libduckdb/duckdb.h /usr/local/include/ && \
    cp /tmp/libduckdb/duckdb.hpp /usr/local/include/ && \
    ldconfig && \
    rm -rf /tmp/libduckdb /tmp/libduckdb.zip

# Configure RobotOps APT repository and install robotops-msgs
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
