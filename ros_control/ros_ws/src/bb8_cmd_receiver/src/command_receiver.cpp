#include "bb8_cmd_receiver/command_receiver.hpp"
#include <iostream>
#include <cstring>

CommandReceiver::CommandReceiver() : Node("command_receiver"), serial_buffer_("")
{
    this->kp = 2.0; // this->declare_parameter<double>("kp", 2.0);
    this->ki = 0.0; // this->declare_parameter<double>("ki", 0.0);
    this->kd = 0.0; // this->declare_parameter<double>("kd", 0.0);

    // RCLCPP_INFO(this->get_logger(), "Parameters declared: kp=%.2f, ki=%.2f, kd=%.2f", kp, ki, kd);

    serial_port_ = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_port_ < 0)
    {
        serial_port_ = open("/dev/ttyUSB1", O_RDWR | O_NOCTTY | O_NONBLOCK);
    }
    if (serial_port_ < 0)
    {
        serial_port_ = open("/dev/ttyACM0", O_RDWR | O_NOCTTY | O_NONBLOCK);
    }
    if (serial_port_ < 0)
    {
        RCLCPP_ERROR(this->get_logger(), "Error %i from open: %s. Checked ttyUSB0, ttyUSB1, ttyACM0", errno, strerror(errno));
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
    tty.c_lflag &= ~ICANON;        // Non-canonical mode
    tty.c_lflag &= ~ECHO;          // Disable echo
    tty.c_lflag &= ~ISIG;          // Disable interpretation of INTR, QUIT and SUSP
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_cc[VMIN] = 0;            // Non-blocking read
    tty.c_cc[VTIME] = 0;           // No timeout

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    if (tcsetattr(serial_port_, TCSANOW, &tty) != 0)
    {
        RCLCPP_ERROR(this->get_logger(), "Error from tcsetattr: %s", strerror(errno));
    }

    RCLCPP_INFO(this->get_logger(), "Serial Bridge Started");

    // Create publishers
    hmi_echo_publisher_ = this->create_publisher<bb8_cmd_receiver::msg::HMICmds>("/hmi_cmds_echo", 10);
    state_publisher_ = this->create_publisher<bb8_cmd_receiver::msg::StateEstimation>("/robot/state_estimation", 10);

    // HMI command callback - forward to serial and echo back
    auto command_callback =
        [this](bb8_cmd_receiver::msg::HMICmds::UniquePtr msg) -> void
    {
        // this->get_parameter("kp", this->kp);
        // this->get_parameter("ki", this->ki);
        // this->get_parameter("kd", this->kd);

        bool ai_mode = msg->ai_mode;
        float head_dir = msg->head_direction;
        float head_force = msg->head_force > 1.0 ? 1.0 : msg->head_force;
        float body_dir = msg->body_direction;
        float body_force = msg->body_force > 1.0 ? 1.0 : msg->body_force;

        RCLCPP_INFO(this->get_logger(), "head_direction: %3.1f,  head_force: %1.3f", msg->head_direction, msg->head_force);
        RCLCPP_INFO(this->get_logger(), "body_direction: %3.1f,  body_force: %1.3f", msg->body_direction, msg->body_force);
        RCLCPP_INFO(this->get_logger(), "AI mode: %d", msg->ai_mode);

        // Format: "ai_mode,head_dir,head_force,body_dir,body_force,kp,ki,kd\n"
        char buffer[128];
        int len = std::sprintf(buffer, "%d,%3.1f,%1.3f,%3.1f,%1.3f,%1.3f,%1.3f,%1.3f\n",
                               ai_mode, head_dir, head_force, body_dir, body_force, this->kp, this->ki, this->kd);

        // Write to serial
        int written = write(serial_port_, buffer, len);

        if (written < 0)
        {
            RCLCPP_ERROR(this->get_logger(), "Failed to write to serial");
        }

        // Echo the command for dashboard consumption
        auto echo_msg = bb8_cmd_receiver::msg::HMICmds();
        echo_msg.ai_mode = ai_mode;
        echo_msg.head_direction = head_dir;
        echo_msg.head_force = head_force;
        echo_msg.body_direction = body_dir;
        echo_msg.body_force = body_force;
        hmi_echo_publisher_->publish(echo_msg);
    };

    auto image_callback =
        [this](std_msgs::msg::String::UniquePtr msg) -> void
    {
        RCLCPP_INFO(this->get_logger(), "I heard: '%s'", msg->data.c_str());
    };

    command_subscription_ = this->create_subscription<bb8_cmd_receiver::msg::HMICmds>("/hmi_cmds", 10, command_callback);
    image_subscription_ = this->create_subscription<std_msgs::msg::String>("/web_cam/compressed", 10, image_callback);
    
    // PID tuning subscription
    pid_subscription_ = this->create_subscription<bb8_cmd_receiver::msg::PIDParams>(
        "/pid_tune", 10,
        std::bind(&CommandReceiver::pid_callback, this, std::placeholders::_1));

    // Timer for reading serial data from ESP32 (10 Hz)
    serial_read_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(100),
        std::bind(&CommandReceiver::serial_read_callback, this));
}


void CommandReceiver::pid_callback(const bb8_cmd_receiver::msg::PIDParams::SharedPtr msg)
{
    RCLCPP_INFO(this->get_logger(), "Received PID params: kp=%.3f, ki=%.3f, kd=%.3f", msg->kp, msg->ki, msg->kd);
    
    // Update ROS parameters for persistence
    // this->set_parameter(rclcpp::Parameter("kp", msg->kp));
    // this->set_parameter(rclcpp::Parameter("ki", msg->ki));
    // this->set_parameter(rclcpp::Parameter("kd", msg->kd));
    
    this->kp = msg->kp;
    this->ki = msg->ki;
    this->kd = msg->kd;
}

void CommandReceiver::serial_read_callback()
{
    char read_buf[256];
    int bytes_read = read(serial_port_, read_buf, sizeof(read_buf) - 1);
    
    if (bytes_read > 0)
    {
        read_buf[bytes_read] = '\0';
        serial_buffer_ += read_buf;
        
        // Process complete lines (ending with \n)
        size_t newline_pos;
        while ((newline_pos = serial_buffer_.find('\n')) != std::string::npos)
        {
            std::string line = serial_buffer_.substr(0, newline_pos);
            serial_buffer_.erase(0, newline_pos + 1);
            
            // Parse ESP32 CSV format:
            // "frw_request,trn_request,incli_goal,incli_error,cur_pitch,cur_yaw,incli_output,out_A,out_B,gyro_y"
            float frw_req, trn_req, incli_goal, incli_error, cur_pitch, cur_yaw, incli_output, out_a, out_b, gyro_y;
            
            int items = sscanf(line.c_str(),
                "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
                &frw_req, &trn_req, &incli_goal, &incli_error, &cur_pitch, &cur_yaw, &incli_output, &out_a, &out_b, &gyro_y);
            
            if (items == 10)
            {
                auto state_msg = bb8_cmd_receiver::msg::StateEstimation();
                state_msg.forward_request = frw_req;
                state_msg.turn_request = trn_req;
                state_msg.inclination_goal = incli_goal;
                state_msg.inclination_error = incli_error;
                state_msg.current_pitch = cur_pitch;
                state_msg.current_yaw = cur_yaw;
                state_msg.inclination_output = incli_output;
                state_msg.motor_a_output = out_a;
                state_msg.motor_b_output = out_b;
                state_msg.gyro_y = gyro_y;
                
                state_publisher_->publish(state_msg);
            }
        }
        
        // Prevent buffer overflow by trimming if too long
        if (serial_buffer_.size() > 1024)
        {
            serial_buffer_.clear();
        }
    }
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