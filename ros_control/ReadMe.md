Start the bridge and Listener node inside the ros-control container:
```
cd /root/ros_ws
colcon build
source install/setup.bash
ros2 launch bb8_cmd_receiver bridge.launch.xml
```
