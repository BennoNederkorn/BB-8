# BB-8 Autonomous Spherical Robot: Containerized Deployment

This document outlines the containerized software architecture for the autonomous spherical robot project. The system is designed for deployment on an Nvidia Jetson platform and is orchestrated using `docker-compose`.

## 1. Architecture Overview

The software architecture is containerized and managed by `docker-compose` to ensure a modular, reproducible, and isolated environment. It consists of two primary services that run in separate containers:

* **`ros-control`**: A container running ROS2. This service is responsible for all high-level control, communication with the smartphone-based HMI, and interfacing with the low-level ESP32 motor controller.
* **`ai-inference`**: A dedicated inference container for AI processing. This service runs on a specialized **NVIDIA L4T (Linux for Tegra)** base image to leverage the Jetson's GPU for hardware-accelerated inference. It handles tasks such as person detection (RF-DETR) and facial recognition (InsightFace).

These two services communicate over a shared Docker bridge network, allowing the `ros-control` node to request analytics data from the `ai-inference` node.

## 2. Prerequisites (Host Setup)

Before launching the application, the host Nvidia Jetson platform must be configured as follows:

1.  **Host OS:** The Jetson device should be flashed with a compatible Linux distribution (e.g., "headless Ubuntu 24.04").
2.  **Docker:** Docker Engine must be installed.
3.  **Docker Compose:** The Docker Compose plugin must be installed.
4.  **NVIDIA Container Toolkit:** This is **critical**. The toolkit must be installed to allow Docker containers to access the host's GPU for CUDA acceleration.
5.  **Hardware Verification:**
    * Ensure the control ESP32 is connected via USB. Identify its device name (e.g., `/dev/ttyUSB0` or `/dev/ttyACM0`) by running `ls /dev/tty*`.
    * Ensure the camera (or a test webcam) is connected and recognized by the host OS (e.g., as `/dev/video0`).

## 3. Project Directory Structure

The repository should be structured as follows to work with the provided configuration files:

```bash
bb8_project/
├── docker-compose.yml       # Main Docker Compose orchestration file
│
├── ros_control/
│   ├── Dockerfile             # Defines the ROS2 container
│   └── ros_ws/                # Your ROS 2 workspace (src, build, log, install)
│
└── ai_inference/
    ├── Dockerfile             # Defines the AI inference container
    ├── requirements.txt       # Python dependencies (PyTorch, InsightFace, etc.)
    └── app/                   # Python scripts for the AI inference API
```
## 4. Configuration Files

Below are the contents of the core configuration files. 

* **docker-compose.yml**: This file defines the two services (`ros-control`, `ai-inference`) and the shared network (`robot_net`).
* **ros_control/Dockerfile**: This Dockerfile builds the ROS2 container, installing rosbridge (for HMI) and micro-ros (for ESP32).
* **ai_inference/Dockerfile**: This Dockerfile builds the AI container from an official NVIDIA L4T-PyTorch image. This base image includes pre-compiled PyTorch and all necessary CUDA/cuDNN libraries for the Jetson, ensuring maximum performance.  

## 5. System & Communication Flow

The two containers operate on the shared robot_net network and interact as follows:

* **AI Node (ai-inference)**:

    * This container runs a simple web server (e.g., Flask) in `app/main.py`.
    * It exposes API endpoints (e.g., `/detect`, `/recognize`).
    * When an endpoint is called, it captures a frame from `/dev/video0`, performs hardware-accelerated inference using the Jetson's GPU, and returns a JSON response (e.g., bounding boxes, recognition IDs).

* **ROS Node (ros-control)**:
    * This container runs the main ROS2 logic.
    * It communicates with the ESP32 controller via the mapped serial device (/dev/ttyUSB0).
    * To get analytics, a ROS node (in Python) makes an HTTP request to the AI node (e.g., requests.get('http://ai-inference:5000/detect')). It can use the service name ai-inference as a hostname.
    * It receives the JSON response and publishes the analytics (e.g., bounding boxes, gestures) as ROS2 topics.

* **Data Flow Summary**:
    * **Control (HMI)**: HMI -> ros-control (via rosbridge) -> ESP32 (via serial)
    * **Control (AI)**: Camera -> ai-inference (via API call) -> ros-control (via HTTP) -> ESP32 (via serial)
    * **Video**: ESP32-S3 Camera -> HMI (via WebRTC)

## 6. Usage
To build and run the complete application stack:

* Build the images:
```bash
docker-compose build
```

* Run the containers (in detached mode):
```bash
docker-compose up -d
```

## 7. Debugging
* View live logs for all services:
```bash
docker-compose logs -f
```

* View logs for a specific service:
```bash
docker-compose logs -f ros-control
docker-compose logs -f ai-inference
```

* Open a bash shell inside a running container:
```bash
# Shell into the ROS container
docker-compose exec ros-control bash

# Shell into the AI container
docker-compose exec ai-inference bash
```

* Stop and remove all containers, networks, and volumes:
```bash
docker-compose down -v
```
