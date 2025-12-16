# Custom Dockerfile for BB-8 AI Inference
ARG BASE_IMAGE=dustynv/ros:humble-pytorch-l4t-r32.7.1
FROM ${BASE_IMAGE}

# ==========================================
# FIX & BUILD CYCLONE DDS (SOURCE BUILD)
# ==========================================

# 1. REMOVE the broken ROS apt repository and install build tools
# We delete /etc/apt/sources.list.d/ros2*.list so apt-get update doesn't fail.
RUN rm -f /etc/apt/sources.list.d/ros2*.list && \
    apt-get update && \
    apt-get install -y \
    cmake \
    build-essential \
    python3-colcon-common-extensions \
    git \
    wget \
    && rm -rf /var/lib/apt/lists/*

# 2. Clone CycloneDDS and RMW adapter
WORKDIR /ros2_cyclone_ws/src
RUN git clone https://github.com/eclipse-cyclonedds/cyclonedds.git && \
    git clone https://github.com/ros2/rmw_cyclonedds.git -b humble

# 3. Build them against the existing ROS Humble install
WORKDIR /ros2_cyclone_ws
RUN /bin/bash -c "source /opt/ros/humble/install/setup.bash && \
    colcon build --merge-install --packages-up-to rmw_cyclonedds_cpp"

# 4. Set Environment Variable to force usage of Cyclone
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp

# ==========================================
# USER CUSTOMIZATIONS
# ==========================================

# Reset working directory
WORKDIR /

# 1. Install websockets
RUN pip3 install websockets

# 2. Install onnxruntime (GPU version for Jetson)
RUN wget https://nvidia.box.com/shared/static/pmsqsiaw4pg9qrbeckcbymho6c01jj4z.whl -O onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    pip3 install onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    rm onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl

# 3. Startup script: Source our new workspace + overlays
RUN printf '%s\n' \
  '#!/bin/bash' \
  'set -e' \
  '' \
  '# Source ROS 2 Humble environment' \
  '# We prioritize the new Cyclone workspace, which chains the base ROS install' \
  'if [ -f "/ros2_cyclone_ws/install/setup.bash" ]; then' \
  '  source "/ros2_cyclone_ws/install/setup.bash"' \
  'elif [ -n "${ROS_DISTRO}" ] && [ -f "/opt/ros/${ROS_DISTRO}/install/setup.bash" ]; then' \
  '  source "/opt/ros/${ROS_DISTRO}/install/setup.bash"' \
  'fi' \
  '' \
  '# Source ros_deep_learning overlay if present' \
  'if [ -f "/ros_deep_learning/install/setup.bash" ]; then' \
  '  source "/ros_deep_learning/install/setup.bash"' \
  'fi' \
  '' \
  '# Generate SSL certificates if missing' \
  'if [ ! -f /jetson-inference/data/key.pem ] || [ ! -f /jetson-inference/data/cert.pem ]; then' \
  '  echo "Generating SSL certificates..."' \
  '  openssl req -x509 -newkey rsa:4096 \\' \
  '    -keyout /jetson-inference/data/key.pem \\' \
  '    -out /jetson-inference/data/cert.pem \\' \
  '    -days 365 -nodes -subj "/CN=localhost"' \
  'fi' \
  '' \
  'export SSL_KEY=/jetson-inference/data/key.pem' \
  'export SSL_CERT=/jetson-inference/data/cert.pem' \
  'echo "SSL_KEY=/jetson-inference/data/key.pem" >> /etc/environment' \
  'echo "SSL_CERT=/jetson-inference/data/cert.pem" >> /etc/environment' \
  '' \
  'exec "$@"' \
  > /start.sh \
  && chmod +x /start.sh
  
ENTRYPOINT ["/start.sh"]
CMD ["/bin/bash"]