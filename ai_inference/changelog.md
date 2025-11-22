# 22.11.2025
## Jetson Backend Configuration
Plan is to leverages the modules found in the ``jetson-inference`` repository to bridge the gap between "Visuals" and "ROS Data."

1. Activate ``ros_deep_learning`` (submodule inside ``jetson-inference``)
The repository contains a module called ros_deep_learning. This is the missing link that converts detectnet visual inferences into ROS topics. Ensure this package is installed in your Kilted workspace.

    ```Bash
    # Inside your ROS 2 workspace (e.g., ~/ros2_ws/src)
    git clone https://github.com/dusty-nv/ros_deep_learning
    cd ..
    colcon build --symlink-install
    ```

2. Create the  "Hybrid" Launch File
We need a single command that starts the Inference Engine, the WebRTC Server, and the ROS Bridge.
    Create: ``sentry_startup.launch.py``. It should have the following logic
    1. Start ``rosbridge_websocket`` on port 9090.
    2. Start ``detectnet`` (from ``ros_deep_learning``).
    3. Pass the argument ``output="webrtc://@:8554/output"`` to the detectnet node.
    
    This argument tells the node to spin up the built-in WebRTC server from jetson-utils automatically.

3. Angular "Hybrid" Client
We will port the vanilla JavaScript from the repository (``webrtc.js``) into a clean Angular Service. We will create ``VideoService`` (The WebRTC Handshake): The jetson-inference WebRTC server expects a specific handshake. Do not write this from scratch.

    * **Source**: Open jetson-utils/python/www/webrtc/webrtc.js in the repo.
    * **Implementation**: Copy the ``onIncomingICE`` and ``onIncomingSDP`` logic into an Angular Service.
