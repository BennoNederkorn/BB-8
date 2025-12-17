ARG BASE_IMAGE=dustynv/ros:humble-pytorch-l4t-r32.7.1

# ==========================================
# STAGE 1: The Builder (Compiling CycloneDDS)
# ==========================================
FROM ${BASE_IMAGE} AS builder

USER root

# FIX: Refresh the expired ROS GPG key
RUN apt-get update || true && \
    apt-get install -y curl gnupg2 lsb-release && \
    curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg && \
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(lsb_release -cs) main" | tee /etc/apt/sources.list.d/ros2.list > /dev/null

# 1. Install Build Dependencies
# We don't clean apt lists here because this layer is discarded
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    python3-colcon-common-extensions \
    git \
    wget

# 2. Clone Source Code
WORKDIR /ros2_cyclone_ws/src
RUN git clone -b 0.10.2 https://github.com/eclipse-cyclonedds/cyclonedds.git && \
    git clone https://github.com/ros2/rmw_cyclonedds.git -b humble

# 3. Build CycloneDDS
# We remove the conflicting libs here temporarily to ensure a clean build
RUN rm -f /opt/ros/humble/lib/libddsc* && \
    rm -f /opt/ros/humble/bin/idlc

WORKDIR /ros2_cyclone_ws
RUN /bin/bash -c "source /opt/ros/humble/install/setup.bash && \
    colcon build --merge-install --packages-up-to rmw_cyclonedds_cpp \
    --cmake-args \
    -DBUILD_TESTING=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_DDSPERF=OFF \
    -DENABLE_SHM=OFF"

# ==========================================
# STAGE 2: The Runtime (Final Image)
# ==========================================
FROM ${BASE_IMAGE}

# 1. Clean Base Image & Prepare Runtime
# We remove the broken repos and conflicting libs here for the final state
RUN rm -f /etc/apt/sources.list.d/ros2*.list && \
    rm -f /opt/ros/humble/lib/libddsc* && \
    rm -f /opt/ros/humble/bin/idlc && \
    apt-get update && \
    # Install runtime dependencies (like openssl for the cert generation)
    apt-get install -y --no-install-recommends \
    openssl \
    wget \
    && rm -rf /var/lib/apt/lists/*

# 2. Copy Build Artifacts from Builder
# This pulls ONLY the compiled binaries, leaving source code and GCC behind
COPY --from=builder /ros2_cyclone_ws/install /ros2_cyclone_ws/install

# 3. Set Environment for Cyclone
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp

# 4. Install Python Dependencies
# Added --no-cache-dir to prevent saving temp files in the layer
RUN pip3 install --no-cache-dir websockets

# 5. Install ONNX Runtime (GPU)
# Chain commands to ensure the large .whl file is deleted in the SAME layer
RUN wget -q https://nvidia.box.com/shared/static/pmsqsiaw4pg9qrbeckcbymho6c01jj4z.whl -O onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    pip3 install --no-cache-dir onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    rm onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl

# 6. Create Entrypoint Script
# (Writing to file in a single step)
WORKDIR /
RUN printf '%s\n' \
  '#!/bin/bash' \
  'set -e' \
  '' \
  '# Source ROS 2 Humble environment' \
  'if [ -f "/ros2_cyclone_ws/install/setup.bash" ]; then' \
  '  source "/ros2_cyclone_ws/install/setup.bash"' \
  'elif [ -n "${ROS_DISTRO}" ] && [ -f "/opt/ros/${ROS_DISTRO}/install/setup.bash" ]; then' \
  '  source "/opt/ros/${ROS_DISTRO}/install/setup.bash"' \
  'fi' \
  '' \
  '# Source ros_deep_learning overlay' \
  'if [ -f "/ros_deep_learning/install/setup.bash" ]; then' \
  '  source "/ros_deep_learning/install/setup.bash"' \
  'fi' \
  '' \
  '# Generate SSL certs if missing' \
  'if [ ! -f /jetson-inference/data/key.pem ] || [ ! -f /jetson-inference/data/cert.pem ]; then' \
  '  echo "Generating SSL certificates..."' \
  '  mkdir -p /jetson-inference/data' \
  '  openssl req -x509 -newkey rsa:4096 \' \
  '    -keyout /jetson-inference/data/key.pem \' \
  '    -out /jetson-inference/data/cert.pem \' \
  '    -days 365 -nodes -subj "/CN=localhost"' \
  'fi' \
  '' \
  'export SSL_KEY=/jetson-inference/data/key.pem' \
  'export SSL_CERT=/jetson-inference/data/cert.pem' \
  '# Only append to /etc/environment if writable (docker best practice)' \
  'if [ -w /etc/environment ]; then' \
  '  echo "SSL_KEY=/jetson-inference/data/key.pem" >> /etc/environment' \
  '  echo "SSL_CERT=/jetson-inference/data/cert.pem" >> /etc/environment' \
  'fi' \
  '' \
  'exec "$@"' \
  > /start.sh && chmod +x /start.sh

ENTRYPOINT ["/start.sh"]
CMD ["/bin/bash"]