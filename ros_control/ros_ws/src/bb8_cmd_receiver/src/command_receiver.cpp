#include "bb8_cmd_receiver/command_receiver.hpp"
#include <iostream>

CommandReceiver::CommandReceiver() : Node("command_receiver")
{
    serial_port_ = open("/dev/ttyUSB0", O_RDWR); // # TODO change if needed, eg to /dev/ttyACM0
    if (serial_port_ < 0)
    {
        serial_port_ = open("/dev/ttyUSB1", O_RDWR);
    }
    if (serial_port_ < 0)
    {
        RCLCPP_ERROR(this->get_logger(), "Error %i from open: %s", errno, strerror(errno));
        return;
    }

    // Configure Serial (115200 baud, 8N1)
    struct termios tty;
    if (tcgetattr(serial_port_, &tty) != 0)
    {
        RCLCPP_ERROR(this->get_logger(), "tcgetattr(serial_port_, &tty) != 0");
        return;
    }

    tty.c_cflag &= ~PARENB; // No parity
    tty.c_cflag &= ~CSTOPB; // One stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 bits per byte
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    if (tcsetattr(serial_port_, TCSANOW, &tty) != 0)
    {
        RCLCPP_ERROR(this->get_logger(), "Error from tcsetattr: %s", strerror(errno));
    }

    // // 2. Create Timer to send data (10Hz)
    // timer_ = this->create_wall_timer(
    //     std::chrono::milliseconds(100),
    //     std::bind(&SerialSender::timer_callback, this));

    RCLCPP_INFO(this->get_logger(), "Serial Bridge Started on /dev/ttyUSB0");

    auto command_callback =
        [this](bb8_cmd_receiver::msg::HMICmds::UniquePtr msg) -> void
    {
        bool ai_mode = msg->ai_mode;
        double head_dir = msg->head_direction;
        double head_force = msg->head_force > 1.0 ? 1.0 : msg->head_force;
        double body_dir = msg->body_direction;
        double body_force = msg->body_force > 1.0 ? 1.0 : msg->body_force;

        RCLCPP_INFO(this->get_logger(), "head_direction: %3.1f,  head_force: %1.3f", msg->head_direction, msg->head_force);
        RCLCPP_INFO(this->get_logger(), "body_direction: %3.1f,  body_force: %1.3f", msg->body_direction, msg->body_force);
        RCLCPP_INFO(this->get_logger(), "AI mode: %d", msg->ai_mode);

        // Format: "1,000.0,0.000,360.0,1.000\n"
        char buffer[128];
        int len = std::sprintf(buffer, "%d,%3.1f,%1.3f,%3.1f,%1.3f\n",
                               ai_mode, head_dir, head_force, body_dir, body_force);

        // Write to serial
        int written = write(serial_port_, buffer, len);

        if (written < 0)
        {
            RCLCPP_ERROR(this->get_logger(), "Failed to write to serial");
        }
    };

    auto image_callback =
        [this](std_msgs::msg::String::UniquePtr msg) -> void
    {
        RCLCPP_INFO(this->get_logger(), "I heard: '%s'", msg->data.c_str());
    };

    command_subscription_ = this->create_subscription<bb8_cmd_receiver::msg::HMICmds>("/hmi_cmds", 10, command_callback);
    image_subscription_ = this->create_subscription<std_msgs::msg::String>("/web_cam/compressed", 10, image_callback);
}

CommandReceiver::~CommandReceiver()
{
    close(serial_port_);
}

int main(int argc, char *argv[])
{
    rclcpp::init(argc, argv);
    std::cout << "Starting command_receiver node..." << std::endl;
    rclcpp::spin(std::make_shared<CommandReceiver>());
    rclcpp::shutdown();
    return 0;
}