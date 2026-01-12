# --- Stage 1: The Base Image ---
# (Same as before - strictly runtime dependencies)
FROM arm64v8/ros:kilted AS base
ENV DEBIAN_FRONTEND=noninteractive

# Install all system-level ROS dependencies
RUN apt-get update && \
    apt-get install -y \
    ros-kilted-ros2-control \
    ros-kilted-ros2-controllers \
    ros-kilted-rosbridge-suite \
    ros-kilted-cv-bridge \
    ros-kilted-image-transport \
    ros-kilted-rmw-cyclonedds-cpp \
    && rm -rf /var/lib/apt/lists/*

# --- Stage 2: The Builder Image ---
# (Temporary stage - thrown away at the end)
FROM base AS builder
SHELL ["/bin/bash", "-c"]

# Install build tools ONLY in this layer (if not in base)
# Assuming 'colcon' is in the base, otherwise install it here.

WORKDIR /root/ros_ws

# Copy local workspace
COPY ./ros_control /root/ros_ws

# BUILD CRITICAL FIX:
# Changed --symlink-install to --merge-install
# This forces physical file copies so we can detach from source code later.
RUN . /opt/ros/kilted/setup.bash && \
    colcon build --merge-install

# --- Stage 3: The Runtime Image (NEW) ---
# Start fresh from the clean base
FROM base AS runner

# Copy ONLY the install folder from the builder
# We leave behind /src, /build, and /log
COPY --from=builder /root/ros_ws/install /root/ros_ws/install

# Setup environment
RUN echo "source /opt/ros/kilted/setup.bash" >> ~/.bashrc
RUN echo "source /root/ros_ws/install/setup.bash" >> ~/.bashrc

CMD ["/bin/bash"]