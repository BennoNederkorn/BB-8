# --- Stage 1: The Base Image ---
# This has all our apt-get dependencies.
FROM arm64v8/ros:kilted AS base

# Set non-interactive mode and source ROS in bash
ENV DEBIAN_FRONTEND=noninteractive
RUN echo "source /opt/ros/kilted/setup.bash" >> ~/.bashrc

# Install all system-level ROS dependencies
RUN apt-get update && \
    apt-get install -y \
    ros-kilted-ros2-control \
    ros-kilted-ros2-controllers \
    ros-kilted-rosbridge-suite \
    ros-kilted-cv-bridge \
    ros-kilted-image-transport \
    ros-kilted-dev-tools \
    && \
    rm -rf /var/lib/apt/lists/*

# --- Stage 2: The Builder Image ---
# This layer builds our local source code
FROM base AS builder

# Create a directory for our workspace
WORKDIR /root/ros_ws

# Copy our local workspace code (from the host) into the container
COPY ./ros_ws /root/ros_ws

# --- THIS IS YOUR SNIPPET (Corrected for Kilted) ---
# Source the ROS environment and build our workspace
RUN . /opt/ros/kilted/setup.bash && \
    colcon build --symlink-install

# --- Final Setup ---
# Source our new workspace's setup file as well
RUN echo "source /root/ros_ws/install/setup.bash" >> ~/.bashrc

# The default command
CMD ["/bin/bash"]