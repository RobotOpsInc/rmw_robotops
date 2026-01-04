# syntax=docker/dockerfile:1.4
FROM ros:jazzy-ros-base

# Install development dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3-colcon-common-extensions \
    python3-rosdep \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install FastDDS (our primary DDS implementation)
RUN apt-get update && apt-get install -y \
    ros-jazzy-rmw-fastrtps-cpp \
    ros-jazzy-fastrtps \
    && rm -rf /var/lib/apt/lists/*

# Configure Cloudsmith repository using buildx secret
# The secret should contain the API key
RUN --mount=type=secret,id=cloudsmith_key \
    CLOUDSMITH_API_KEY=$(cat /run/secrets/cloudsmith_key) && \
    curl -u "kristoph-matthews:${CLOUDSMITH_API_KEY}" -1sLf \
    'https://dl.cloudsmith.io/basic/robotops/robotops-development/setup.deb.sh' \
    | bash

# Install robotops_msgs from Cloudsmith
RUN apt-get update && apt-get install -y \
    ros-jazzy-robotops-msgs=0.1.0-0noble \
    && rm -rf /var/lib/apt/lists/*

# Set up workspace
WORKDIR /workspace

# Copy package files
COPY package.xml /workspace/src/rmw_robotops/
COPY CMakeLists.txt /workspace/src/rmw_robotops/
COPY src/ /workspace/src/rmw_robotops/src/
COPY include/ /workspace/src/rmw_robotops/include/
COPY test/ /workspace/src/rmw_robotops/test/

# Source ROS and build
RUN . /opt/ros/jazzy/setup.sh && \
    colcon build \
    --cmake-args \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Source the workspace
RUN echo "source /opt/ros/jazzy/setup.bash" >> ~/.bashrc && \
    echo "source /workspace/install/setup.bash" >> ~/.bashrc

CMD ["/bin/bash"]
