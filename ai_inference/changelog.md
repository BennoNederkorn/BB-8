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

    **Note 1**: We need to do this because the ``jetson-inference`` repository by itself does not speak ROS. The ``ros_deep_learning`` package acts as a wrapper that wraps the raw inference enginee inside a ROS Node. This is the intended data flow with this package installed:
    1. Camera -> Raw Image
    2. Jetson Inference -> Detects "Person" at [x:50, y:100]
    3. ``ros_deep_learning`` Node -> Translates detection into a ROS Message ``(vision_msgs/Detection2DArray)`` and publishes it to ``/detectnet/detections``. 
    4. rosbridge -> Converts that ROS message into JSON.
    5. Angular Dashboard -> Receives JSON and draws the red box on the screen

    **Note 2**: We have to build it, we compile a specific version of this wrapper that matches the Jetson Nano environment. 

2. Create the  "Hybrid" Launch File
We need a single command that starts the Inference Engine, the WebRTC Server, and the ROS Bridge.
    Create: ``sentry_startup.launch.py``. It should have the following logic
    1. Start ``rosbridge_websocket`` on port 9090.
    2. Start ``detectnet`` (from ``ros_deep_learning``).
    3. Pass the argument ``output="webrtc://@:8554/output"`` to the detectnet node.
    
    This argument tells the node to spin up the built-in WebRTC server from jetson-utils automatically.

3. Angular "Hybrid" Client
We will port the vanilla JavaScript from the repository (``webrtc.js``) into a clean Angular Service. We will create ``VideoService`` (The WebRTC Handshake): The jetson-inference WebRTC server expects a specific handshake. Do not write this from scratch.

    * **Source**: Open ``jetson-utils/python/www/webrtc/webrtc.js`` in the repo.
    * **Implementation**: Copy the ``onIncomingICE`` and ``onIncomingSDP`` logic into an Angular Service.

4. Verify the message structure of the Kilted version of ``ros_deep_learning``. Does it even support the Kilted distribution?
    1. Run the container
    2. Start ``detectnet``
    3. On the Jetson, run
        ```bash
        ros2 topic info /detectnet/detections
        ros2 interface show <the_type_returned_above>
        ```