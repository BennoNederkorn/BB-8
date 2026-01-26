#ifndef BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_
#define BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_

#include <memory>
#include <unistd.h>  // write(), read(), close()
#include <fcntl.h>   // Contains file controls like O_RDWR
#include <errno.h>   // Error integer and strerror
#include <termios.h> // POSIX terminal control definitions

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "bb8_cmd_receiver/msg/hmi_cmds.hpp"

class CommandReceiver : public rclcpp::Node
{
public:
    CommandReceiver();
    ~CommandReceiver();

private:
    rclcpp::Subscription<bb8_cmd_receiver::msg::HMICmds>::SharedPtr command_subscription_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr image_subscription_;
    int serial_port_;
    double kp, ki, kd;
    // rclcpp::TimerBase::SharedPtr timer_;
};

#endif // BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_