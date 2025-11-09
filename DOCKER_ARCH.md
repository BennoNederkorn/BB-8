# BB-8 Autonomous Spherical Robot: Containerized Deployment

This document outlines the containerized software architecture for the autonomous spherical robot project. The system is designed for deployment on an Nvidia Jetson platform and is orchestrated using `docker-compose`.

## 1. Architecture Overview

The software architecture is containerized and managed by `docker-compose` to ensure a modular, reproducible, and isolated environment. It consists of a ROS2 control service and one or more AI inference services that run in separate containers:

* **`ros-control`**: A container running ROS2. This service is responsible for all high-level control, communication with the smartphone-based HMI, and interfacing with the low-level ESP32 motor controller.
* **AI inference services (`ai-*`)**: One or more dedicated inference containers under `ai_inference/<subdir>` for specific models/applications (e.g., person detection, pose estimation, face recognition). Each subdirectory contains its own Dockerfile and optional docker scripts. These services run on specialized **NVIDIA L4T (Linux for Tegra)** base images to leverage the Jetson's GPU for hardware-accelerated inference.

The services communicate over a shared Docker bridge network, allowing `ros-control` to request analytics data from the desired AI service by addressing it by service name (e.g., `http://ai-inference:5000/detect`).

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

The repository is structured to support multiple AI inference containers. Each AI application lives under its own subdirectory in `ai_inference/` with its own Dockerfile (and optional docker helper scripts):

```bash
bb8_project/
├── docker-compose.yml               # Main Docker Compose orchestration file
│
├── ros_control/
│   ├── Dockerfile                   # Defines the ROS2 container
│   └── ros_ws/                      # Your ROS 2 workspace (src, build, log, install)
│
└── ai_inference/
    ├── jetson-inference/            # NVIDIA jetson-inference based container
    │   ├── Dockerfile
    │   └── docker/                  # Helper scripts (build.sh, run.sh, ...)
    │
    ├── <another-model-or-app>/      # Future AI container(s)
    │   └── Dockerfile               # Each AI container provides its own image
    │
    └── ...
```
## 4. Configuration Files

Below are the contents of the core configuration files. 

* **docker-compose.yml**: This file defines `ros-control` and one or more AI services (e.g., `ai-inference`) and the shared network (`robot_net`). Each AI service points its build context to a different subdirectory under `ai_inference/`.
* **ros_control/Dockerfile**: Builds the ROS2 container, installing rosbridge (for HMI) and micro-ros (for ESP32).
* **ai_inference/<subdir>/Dockerfile**: Builds an AI container from an appropriate NVIDIA L4T base image (e.g., the provided `ai_inference/jetson-inference/Dockerfile`). These base images include pre-compiled PyTorch and CUDA/cuDNN libraries for Jetson.

## 5. System & Communication Flow

The containers operate on the shared `robot_net` network and interact as follows:

* **AI Node(s) (`ai-*`)**:

    * Each AI container can expose its own API (e.g., Flask/FastAPI or gRPC) with endpoints like `/detect` or `/recognize`.
    * When an endpoint is called, it captures a frame from `/dev/video0`, performs hardware-accelerated inference using the Jetson's GPU, and returns a JSON response (e.g., bounding boxes, recognition IDs).
    * The provided `ai_inference/jetson-inference` image compiles and ships NVIDIA's jetson-inference toolset. You can mount `ai_inference/jetson-inference/data` to persist downloaded models and training datasets.

* **ROS Node (ros-control)**:
    * Runs the main ROS2 logic.
    * Communicates with the ESP32 controller via the mapped serial device (/dev/ttyUSB0).
    * To get analytics, a ROS node (in Python) makes an HTTP request to the appropriate AI service (e.g., `requests.get('http://ai-inference:5000/detect')`). It can use the service name as the hostname.
    * It receives the JSON response and publishes the analytics (e.g., bounding boxes, gestures) as ROS2 topics.

* **Data Flow Summary**:
    * **Control (HMI)**: HMI -> ros-control (via rosbridge) -> ESP32 (via serial)
    * **Control (AI)**: Camera -> AI service (via HTTP) -> ros-control (via HTTP) -> ESP32 (via serial)
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

# Shell into the default AI container
docker-compose exec ai-inference bash

# If you add more AI containers, replace the service name accordingly
# docker-compose exec ai-<your-service> bash
```

* Stop and remove all containers, networks, and volumes:
```bash
docker-compose down -v
```
