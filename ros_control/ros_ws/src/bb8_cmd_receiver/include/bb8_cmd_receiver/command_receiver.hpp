#ifndef BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_
#define BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_

#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "bb8_cmd_receiver/msg/hmi_cmds.hpp"

class CommandReceiver : public rclcpp::Node
{
public:
    CommandReceiver();

private:
    rclcpp::Subscription<bb8_cmd_receiver::msg::HMICmds>::SharedPtr command_subscription_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr image_subscription_;
};

#endif // BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_