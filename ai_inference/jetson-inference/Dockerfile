# Custom Dockerfile for BB-8 AI Inference
# Extends the ROS2 Humble-enabled image selected by docker/run.sh --ros humble
ARG BASE_IMAGE=dustynv/ros:humble-pytorch-l4t-r32.7.1
FROM ${BASE_IMAGE}

# Install CycloneDDS RMW to avoid FastDDS interop crashes
RUN apt-get update && \
  apt-get install -y ros-humble-rmw-cyclonedds-cpp && \
  rm -rf /var/lib/apt/lists/*

# 1. Install websockets
RUN pip3 install websockets

# 2. Install onnxruntime (GPU version for Jetson)
RUN wget https://nvidia.box.com/shared/static/pmsqsiaw4pg9qrbeckcbymho6c01jj4z.whl -O onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    pip3 install onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl && \
    rm onnxruntime_gpu-1.11.0-cp36-cp36m-linux_aarch64.whl

# 3. Startup script: source ROS humble + ros_deep_learning overlays (like run.sh output) + SSL certs
RUN printf '%s\n' \
  '#!/bin/bash' \
  'set -e' \
  '' \
  '# Source ROS 2 Humble environment (paths vary by image)' \
  'if [ -n "${ROS_DISTRO}" ] && [ -f "/opt/ros/${ROS_DISTRO}/install/setup.bash" ]; then' \
  '  # shellcheck disable=SC1090' \
  '  source "/opt/ros/${ROS_DISTRO}/install/setup.bash"' \
  'elif [ -n "${ROS_DISTRO}" ] && [ -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]; then' \
  '  # shellcheck disable=SC1090' \
  '  source "/opt/ros/${ROS_DISTRO}/setup.bash"' \
  'fi' \
  '' \
  '# Source ros_deep_learning overlay if present (matches run.sh behavior)' \
  'if [ -f "/ros_deep_learning/install/setup.bash" ]; then' \
  '  # shellcheck disable=SC1091' \
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




