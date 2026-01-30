#ifndef BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_
#define BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_

#include <memory>
#include <string>
#include <unistd.h>  // write(), read(), close()
#include <fcntl.h>   // Contains file controls like O_RDWR
#include <errno.h>   // Error integer and strerror
#include <termios.h> // POSIX terminal control definitions

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "bb8_cmd_receiver/msg/hmi_cmds.hpp"
#include "bb8_cmd_receiver/msg/state_estimation.hpp"
#include "bb8_cmd_receiver/msg/pid_params.hpp"

class CommandReceiver : public rclcpp::Node
{
public:
    CommandReceiver();
    ~CommandReceiver();

private:
    void serial_read_callback();
    void pid_callback(const bb8_cmd_receiver::msg::PIDParams::SharedPtr msg);

    rclcpp::Subscription<bb8_cmd_receiver::msg::HMICmds>::SharedPtr command_subscription_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr image_subscription_;
    rclcpp::Subscription<bb8_cmd_receiver::msg::PIDParams>::SharedPtr pid_subscription_;
    
    rclcpp::Publisher<bb8_cmd_receiver::msg::HMICmds>::SharedPtr hmi_echo_publisher_;
    rclcpp::Publisher<bb8_cmd_receiver::msg::StateEstimation>::SharedPtr state_publisher_;
    
    rclcpp::TimerBase::SharedPtr serial_read_timer_;
    
    int serial_port_;
    float kp, ki, kd;
    std::string serial_buffer_;
    // rclcpp::TimerBase::SharedPtr timer_;
    
};

#endif // BB8_CMD_RECEIVER__COMMAND_RECEIVER_HPP_