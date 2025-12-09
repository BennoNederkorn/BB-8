#!/bin/bash

# 1. Create the device model file on the host
# This is required because Docker cannot bind-mount the virtual /proc file directly.
if [ -f /proc/device-tree/model ]; then
    echo "Detected Jetson device. Copying model info to /tmp/nv_jetson_model..."
    cat /proc/device-tree/model > /tmp/nv_jetson_model
else
    echo "Warning: /proc/device-tree/model not found (are you on a Jetson?)."
    echo "Creating dummy model file to prevent mount errors..."
    echo "NVIDIA Jetson Nano Developer Kit" > /tmp/nv_jetson_model
fi

# 2. Start the Docker Compose services
echo "Starting Docker Compose..."
docker-compose up
