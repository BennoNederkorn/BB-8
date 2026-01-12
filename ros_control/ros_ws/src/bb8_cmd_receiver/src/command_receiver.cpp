#include "bb8_cmd_receiver/command_receiver.hpp"
#include <iostream>

CommandReceiver::CommandReceiver() : Node("command_receiver")
{
    auto command_callback =
        [this](bb8_cmd_receiver::msg::HMICmds::UniquePtr msg) -> void
    {
        RCLCPP_INFO(this->get_logger(), "head_direction: %.1f,  head_force: %.2f", msg->head_direction, msg->head_force);
        RCLCPP_INFO(this->get_logger(), "body_direction: %.1f,  body_force: %.2f", msg->body_direction, msg->body_force);
        RCLCPP_INFO(this->get_logger(), "AI mode: %d", msg->ai_mode);
    };

    auto image_callback =
        [this](std_msgs::msg::String::UniquePtr msg) -> void
    {
        RCLCPP_INFO(this->get_logger(), "I heard: '%s'", msg->data.c_str());
    };

    command_subscription_ = this->create_subscription<bb8_cmd_receiver::msg::HMICmds>("/hmi_cmds", 10, command_callback);
    image_subscription_ = this->create_subscription<std_msgs::msg::String>("/web_cam/compressed", 10, image_callback);
}

int main(int argc, char *argv[])
{
    rclcpp::init(argc, argv);
    std::cout << "Starting command_receiver node..." << std::endl;
    rclcpp::spin(std::make_shared<CommandReceiver>());
    rclcpp::shutdown();
    return 0;
}