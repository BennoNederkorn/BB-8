cmake_minimum_required(VERSION 3.20)
project(bb8_cmd_receiver)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

# Generate ROS 2 interfaces (messages)
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/HMICmds.msg"
  "msg/StateEstimation.msg"
  "msg/PIDParams.msg"
  "msg/ControlParams.msg"
  "msg/IMUReset.msg"
  DEPENDENCIES std_msgs
)

# Build the command_receiver executable
add_executable(command_receiver src/command_receiver.cpp)

target_include_directories(command_receiver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(command_receiver PUBLIC rclcpp::rclcpp ${std_msgs_TARGETS})
# Link against the generated messages in the same package
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(command_receiver PUBLIC ${cpp_typesupport_target})

# Install launch files, headers, targets, and config
install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}/
)
install(DIRECTORY 
  include/
  DESTINATION include
)
install(TARGETS
  command_receiver
  DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY
  config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
