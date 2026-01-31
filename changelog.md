# BB-8 Project Changelog

## [2026-01-30] Real-Time Telemetry Visualization & PID Tuning System

**Contributors:** Zhuole Lee, with AI assistance from GitHub Copilot (Claude Opus 4.5)

### Overview

This changelog entry documents a comprehensive programming session that established a real-time telemetry visualization system for the BB-8 robot. The modifications span across four major components of the system:

1. **Sentry Dashboard** (Angular 21 frontend)
2. **ROS2 Command Receiver** (C++ node on Jetson Nano)
3. **ESP32 Motor Controller** (PlatformIO/Arduino firmware)
4. **Communication Infrastructure** (WebSocket, rosbridge, Serial UART)

The primary objectives achieved were:
- Real-time visualization of robot state estimation data on the Sentry Dashboard
- Real-time visualization of HMI (Human-Machine Interface) input commands
- Implementation of a PID controller tuning interface accessible from the dashboard
- Establishment of bidirectional communication between all system components

---

### System Architecture

#### High-Level Communication Topology

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                    SENTRY DASHBOARD                                      │
│                                   (Angular 21 Frontend)                                  │
│                                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────────────────┐  │
│  │   PID TUNING CARD   │  │  INPUT COMMANDS     │  │   STATE ESTIMATION SECTION      │  │
│  │  ┌───┐ ┌───┐ ┌───┐  │  │  (Collapsible)      │  │   (Collapsible)                 │  │
│  │  │Kp │ │Ki │ │Kd │  │  │  • Body Direction   │  │   • Current Pitch               │  │
│  │  └───┘ └───┘ └───┘  │  │  • Body Force       │  │   • Current Yaw                 │  │
│  │     [SEND PID]      │  │  • Head Direction   │  │   • Inclination Error           │  │
│  └─────────────────────┘  │  • Head Force       │  │   • Motor A/B Output            │  │
│                           └─────────────────────┘  └─────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────────────┘
           │                              ▲                              ▲
           │ PID Commands                 │ HMI Echo                     │ State Estimation
           │ (WebSocket)                  │ (WebSocket)                  │ (WebSocket)
           ▼                              │                              │
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              JETSON NANO (Docker Container)                              │
│                                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐    │
│  │                    INFERENCE SERVER (Port 9090)                                  │    │
│  │                    • /system/status (CPU, GPU, RAM telemetry)                    │    │
│  │                    • /camera/face_events (face recognition)                      │    │
│  └─────────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐    │
│  │                    ROSBRIDGE WEBSOCKET (Port 9091)                               │    │
│  │                    • Bridges WebSocket ↔ ROS2 topics                             │    │
│  │                    • /hmi_cmds_echo, /robot/state_estimation, /pid_tune          │    │
│  └─────────────────────────────────────────────────────────────────────────────────┘    │
│                                          │                                               │
│                                          │ ROS2 Topics                                   │
│                                          ▼                                               │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐    │
│  │                    COMMAND RECEIVER NODE (bb8_cmd_receiver)                      │    │
│  │                                                                                   │    │
│  │  Subscriptions:                      Publishers:                                  │    │
│  │  • /hmi_cmds (from HMI joystick)     • /hmi_cmds_echo (to dashboard)             │    │
│  │  • /pid_tune (from dashboard)        • /robot/state_estimation (to dashboard)    │    │
│  │                                                                                   │    │
│  │  Serial I/O:                                                                      │    │
│  │  • Writes: "ai,head_dir,head_force,body_dir,body_force,kp,ki,kd\n"               │    │
│  │  • Reads:  "frw,trn,incli_goal,incli_err,pitch,yaw,incli_out,mot_a,mot_b\n"      │    │
│  └─────────────────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────────────────┘
                                          │
                                          │ Serial UART (115200 baud, USB)
                                          ▼
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                   ESP32 MICROCONTROLLER                                  │
│                                                                                          │
│  ┌──────────────────────────────────┐  ┌──────────────────────────────────────────────┐ │
│  │        CORE 0 (loop())           │  │           CORE 1 (controlTask)               │ │
│  │                                  │  │                                              │ │
│  │  • Serial parsing (sscanf)       │  │  • IMU reading (MPU6050)                     │ │
│  │  • PID value updates             │  │  • PID calculation                           │ │
│  │  • Telemetry transmission        │  │  • Motor PWM output                          │ │
│  │  • Head stepper control          │  │  • 100 Hz control loop                       │ │
│  └──────────────────────────────────┘  └──────────────────────────────────────────────┘ │
│                                          │                                               │
│                                          │ MCPWM                                         │
│                                          ▼                                               │
│  ┌─────────────────────────────────────────────────────────────────────────────────┐    │
│  │                           H-BRIDGE MOTOR DRIVERS                                 │    │
│  │                         Motor A (Left)    Motor B (Right)                        │    │
│  └─────────────────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

### Detailed Modifications

#### 1. ROS2 Custom Message Definitions

##### 1.1 StateEstimation.msg (NEW FILE)

**File:** `ros_control/ros_ws/src/bb8_cmd_receiver/msg/StateEstimation.msg`

**Rationale:** The ESP32 continuously computes state estimation values during its PID control loop. These values are essential for debugging and tuning the balance controller. A dedicated ROS2 message type was created to transport this data from the Jetson to the dashboard.

```
float32 forward_request      # Commanded forward velocity (0.0 to 1.0)
float32 turn_request         # Commanded turn rate (-1.0 to 1.0)
float32 inclination_goal     # Target pitch angle in degrees
float32 inclination_error    # Difference between goal and actual pitch
float32 current_pitch        # Current pitch angle from IMU (degrees)
float32 current_yaw          # Current yaw angle from IMU (degrees)
float32 inclination_output   # PID controller output
float32 motor_a_output       # Left motor PWM duty cycle (-100 to 100)
float32 motor_b_output       # Right motor PWM duty cycle (-100 to 100)
```

**Design Decision - float32 vs float64:** Initially, `float64` was considered to match ROS2 conventions. However, `float32` was chosen to:
- Reduce serial bandwidth (4 bytes vs 8 bytes per value)
- Match the ESP32's native `float` type (single precision)
- Provide sufficient precision for motor control (~7 significant digits)

##### 1.2 PIDParams.msg (NEW FILE)

**File:** `ros_control/ros_ws/src/bb8_cmd_receiver/msg/PIDParams.msg`

**Rationale:** Enable real-time PID tuning from the dashboard without reflashing the ESP32 firmware.

```
float32 kp    # Proportional gain
float32 ki    # Integral gain
float32 kd    # Derivative gain
```

##### 1.3 CMakeLists.txt Modification

**File:** `ros_control/ros_ws/src/bb8_cmd_receiver/CMakeLists.txt`

The `rosidl_generate_interfaces` call was updated to include the new message types:

```cmake
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/HMICmds.msg"
  "msg/StateEstimation.msg"    # Added
  "msg/PIDParams.msg"          # Added
)
```

---

#### 2. Command Receiver Node Modifications

##### 2.1 Header File Updates

**File:** `ros_control/ros_ws/src/bb8_cmd_receiver/include/bb8_cmd_receiver/command_receiver.hpp`

**New includes:**
```cpp
#include "bb8_cmd_receiver/msg/state_estimation.hpp"
#include "bb8_cmd_receiver/msg/pid_params.hpp"
```

**New member variables:**
```cpp
// Subscriptions
rclcpp::Subscription<bb8_cmd_receiver::msg::PIDParams>::SharedPtr pid_subscription_;

// Publishers
rclcpp::Publisher<bb8_cmd_receiver::msg::HMICmds>::SharedPtr hmi_echo_publisher_;
rclcpp::Publisher<bb8_cmd_receiver::msg::StateEstimation>::SharedPtr state_publisher_;

// Timer for non-blocking serial reads
rclcpp::TimerBase::SharedPtr serial_read_timer_;

// Serial buffer for accumulating partial reads
std::string serial_buffer_;

// PID parameters (sent to ESP32 with each HMI command)
float kp, ki, kd;
```

##### 2.2 Serial Port Configuration

**File:** `ros_control/ros_ws/src/bb8_cmd_receiver/src/command_receiver.cpp`

**Problem:** The original serial configuration used blocking reads, which would halt the ROS2 node while waiting for data.

**Solution:** Non-blocking serial I/O with a timer-based polling mechanism.

```cpp
// Open with O_NONBLOCK flag
serial_port_ = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NONBLOCK);

// Configure terminal settings
struct termios tty;
tcgetattr(serial_port_, &tty);

tty.c_cflag &= ~PARENB;         // No parity
tty.c_cflag &= ~CSTOPB;         // One stop bit
tty.c_cflag &= ~CSIZE;
tty.c_cflag |= CS8;             // 8 bits per byte
tty.c_cflag |= CREAD | CLOCAL;  // Enable receiver, ignore modem control
tty.c_lflag &= ~ICANON;         // Non-canonical mode (don't wait for newline)
tty.c_lflag &= ~ECHO;           // Disable echo
tty.c_lflag &= ~ISIG;           // Disable signal characters (Ctrl+C, etc.)
tty.c_iflag &= ~(IXON | IXOFF | IXANY);  // Disable software flow control
tty.c_cc[VMIN] = 0;             // Non-blocking: return immediately
tty.c_cc[VTIME] = 0;            // No timeout

cfsetispeed(&tty, B115200);
cfsetospeed(&tty, B115200);
tcsetattr(serial_port_, TCSANOW, &tty);
```

**Explanation of termios settings:**

| Setting | Purpose |
|---------|---------|
| `~ICANON` | Disables line buffering; bytes are available immediately |
| `~ECHO` | Prevents received bytes from being echoed back |
| `~ISIG` | Prevents Ctrl+C (0x03) from being interpreted as SIGINT |
| `~IXON/IXOFF` | Disables XON/XOFF flow control (could misinterpret data bytes) |
| `VMIN=0, VTIME=0` | `read()` returns immediately with whatever is available |

##### 2.3 HMI Echo Publisher

**Rationale:** The dashboard needs to visualize the commands being sent to the robot. However, subscribing directly to `/hmi_cmds` from the dashboard would create timing issues and potential circular dependencies. Instead, the command receiver "echoes" each received command to a separate topic.

```cpp
// In constructor
hmi_echo_publisher_ = this->create_publisher<bb8_cmd_receiver::msg::HMICmds>("/hmi_cmds_echo", 10);

// In command_callback (after writing to serial)
auto echo_msg = bb8_cmd_receiver::msg::HMICmds();
echo_msg.ai_mode = ai_mode;
echo_msg.head_direction = head_dir;
echo_msg.head_force = head_force;
echo_msg.body_direction = body_dir;
echo_msg.body_force = body_force;
hmi_echo_publisher_->publish(echo_msg);
```

##### 2.4 Serial Read Callback (Timer-Based)

**Rationale:** The ESP32 sends state estimation data continuously at 20 Hz. The ROS node must read this data without blocking other operations.

```cpp
// Timer fires every 100ms (10 Hz polling)
serial_read_timer_ = this->create_wall_timer(
    std::chrono::milliseconds(100),
    std::bind(&CommandReceiver::serial_read_callback, this));

void CommandReceiver::serial_read_callback()
{
    char read_buf[256];
    int bytes_read = read(serial_port_, read_buf, sizeof(read_buf) - 1);
    
    if (bytes_read > 0)
    {
        read_buf[bytes_read] = '\0';
        serial_buffer_ += read_buf;
        
        // Process complete lines (CSV format ends with \n)
        size_t newline_pos;
        while ((newline_pos = serial_buffer_.find('\n')) != std::string::npos)
        {
            std::string line = serial_buffer_.substr(0, newline_pos);
            serial_buffer_.erase(0, newline_pos + 1);
            
            // Parse CSV: "frw,trn,goal,err,pitch,yaw,out,mot_a,mot_b"
            float frw_req, trn_req, incli_goal, incli_error;
            float cur_pitch, cur_yaw, incli_output, out_a, out_b;
            
            int items = sscanf(line.c_str(),
                "%f,%f,%f,%f,%f,%f,%f,%f,%f",
                &frw_req, &trn_req, &incli_goal, &incli_error,
                &cur_pitch, &cur_yaw, &incli_output, &out_a, &out_b);
            
            if (items == 9)
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
                
                state_publisher_->publish(state_msg);
            }
        }
        
        // Prevent buffer overflow
        if (serial_buffer_.size() > 1024)
        {
            serial_buffer_.clear();
        }
    }
}
```

##### 2.5 PID Callback

**Rationale:** Allow the dashboard to update PID gains without reflashing firmware.

```cpp
pid_subscription_ = this->create_subscription<bb8_cmd_receiver::msg::PIDParams>(
    "/pid_tune", 10,
    std::bind(&CommandReceiver::pid_callback, this, std::placeholders::_1));

void CommandReceiver::pid_callback(const bb8_cmd_receiver::msg::PIDParams::SharedPtr msg)
{
    RCLCPP_INFO(this->get_logger(), "Received PID params: kp=%.3f, ki=%.3f, kd=%.3f",
                msg->kp, msg->ki, msg->kd);
    
    this->kp = msg->kp;
    this->ki = msg->ki;
    this->kd = msg->kd;
}
```

**Note:** The PID values are sent to the ESP32 as part of the next HMI command packet. This means PID updates are not immediate but are applied when the next joystick input is received.

---

#### 3. Sentry Dashboard Modifications

##### 3.1 Environment Configuration

**File:** `SentryDashboard/sentry-dashboard/src/environments/environment.ts`

**Problem:** The system required two separate WebSocket connections:
1. **Inference Server (port 9090):** Handles face recognition and system telemetry
2. **Rosbridge (port 9091):** Handles ROS2 topic bridging for HMI commands and state estimation

**Solution:** Add a separate URL for rosbridge:

```typescript
export const environment = {
    production: false,
    // Inference server (telemetry, face events)
    wsUrl: 'ws://100.93.171.127:9090',
    
    // Rosbridge WebSocket (HMI commands, state estimation, PID tuning)
    rosbridgeUrl: 'ws://100.93.171.127:9091',
    
    // WebRTC signaling for video
    webrtcUrl: 'ws://100.93.171.127:8554/output'
};
```

##### 3.2 TypeScript Model Definitions

**File:** `SentryDashboard/sentry-dashboard/src/app/models.ts`

```typescript
// HMI Commands sent to BB-8
export interface HMICommands {
    ai_mode: boolean;
    head_direction: number;
    head_force: number;
    body_direction: number;
    body_force: number;
}

// State estimation from ESP32
export interface StateEstimation {
    forward_request: number;
    turn_request: number;
    inclination_goal: number;
    inclination_error: number;
    current_pitch: number;
    current_yaw: number;
    inclination_output: number;
    motor_a_output: number;
    motor_b_output: number;
}

// PID tuning parameters
export interface PIDParams {
    kp: number;
    ki: number;
    kd: number;
}
```

##### 3.3 Robot Communications Service

**File:** `SentryDashboard/sentry-dashboard/src/app/services/robot-comms.service.ts`

**Major Changes:**
1. Split single WebSocket into two separate connections
2. Implemented rosbridge protocol (messages require `op` field)
3. Added automatic topic subscription on connect

```typescript
// Rosbridge protocol message format
interface RosbridgeMessage {
    op: string;           // 'subscribe', 'publish', etc.
    topic?: string;       // ROS topic name
    msg?: any;            // Message payload
    type?: string;        // Message type for subscriptions
}

@Injectable({ providedIn: 'root' })
export class RobotCommsService {
    // Two separate WebSocket connections
    private inferenceSocket$: WebSocketSubject<RobotMessage> | null = null;
    private rosbridgeSocket$: WebSocketSubject<RosbridgeMessage> | null = null;
    
    // Connection status observables
    public readonly connectionStatus$: Observable<string>;      // Inference server
    public readonly rosbridgeStatus$: Observable<string>;       // Rosbridge
    
    // Data observables
    public readonly telemetry$: Observable<SystemStatus>;       // From inference
    public readonly faceEvents$: Observable<FaceEvent>;         // From inference
    public readonly hmiCommands$: Observable<HMICommands>;      // From rosbridge
    public readonly stateEstimation$: Observable<StateEstimation>;  // From rosbridge
    
    constructor() {
        // Initialize both sockets
        this.inferenceSocket$ = this.createInferenceSocket(environment.wsUrl, ...);
        this.rosbridgeSocket$ = this.createRosbridgeSocket(environment.rosbridgeUrl, ...);
        
        // Filter rosbridge messages by topic
        this.hmiCommands$ = rosbridgeShared$.pipe(
            filter((msg) => msg.op === 'publish' && msg.topic === '/hmi_cmds_echo'),
            map((msg) => msg.msg as HMICommands)
        );
        
        this.stateEstimation$ = rosbridgeShared$.pipe(
            filter((msg) => msg.op === 'publish' && msg.topic === '/robot/state_estimation'),
            map((msg) => msg.msg as StateEstimation)
        );
        
        // Subscribe to topics after connection
        this.subscribeToRosbridgeTopics();
    }
    
    private subscribeToRosbridgeTopics(): void {
        this.rosbridgeStatus$.pipe(
            filter((status) => status === 'CONNECTED')
        ).subscribe(() => {
            // Subscribe to HMI commands echo
            this.rosbridgeSocket$?.next({
                op: 'subscribe',
                topic: '/hmi_cmds_echo',
                type: 'bb8_cmd_receiver/msg/HMICmds'
            });
            
            // Subscribe to state estimation
            this.rosbridgeSocket$?.next({
                op: 'subscribe',
                topic: '/robot/state_estimation',
                type: 'bb8_cmd_receiver/msg/StateEstimation'
            });
        });
    }
    
    public sendPIDParams(params: PIDParams): void {
        // Use rosbridge protocol to publish
        this.rosbridgeSocket$?.next({
            op: 'publish',
            topic: '/pid_tune',
            msg: { kp: params.kp, ki: params.ki, kd: params.kd }
        });
    }
}
```

**Rosbridge Protocol Explanation:**

The rosbridge WebSocket server expects JSON messages with a specific format:

| Operation | Format | Description |
|-----------|--------|-------------|
| Subscribe | `{"op": "subscribe", "topic": "/topic_name", "type": "pkg/msg/Type"}` | Start receiving messages |
| Publish | `{"op": "publish", "topic": "/topic_name", "msg": {...}}` | Send a message |
| Unsubscribe | `{"op": "unsubscribe", "topic": "/topic_name"}` | Stop receiving messages |

**Initial Bug:** Messages were sent without the `op` field, causing rosbridge to reject them with:
```
[ERROR] Received a message without an op. All messages require 'op' field...
```

##### 3.4 Dashboard Component

**File:** `SentryDashboard/sentry-dashboard/src/app/components/dashboard/dashboard.component.ts`

**New imports:**
```typescript
import { FormsModule } from '@angular/forms';  // Required for [(ngModel)]
```

**New chart data arrays:**
```typescript
// HMI Input Commands
bodyDirData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Body Direction', line: { color: '#00eaff', width: 2 } }];
bodyForceData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Body Force', line: { color: '#22c55e', width: 2 } }];
headDirData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Head Direction', line: { color: '#ff7b00', width: 2 } }];
headForceData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Head Force', line: { color: '#a855f7', width: 2 } }];

// State Estimation
pitchData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Current Pitch', line: { color: '#00eaff', width: 2 } }];
yawData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Current Yaw', line: { color: '#eab308', width: 2 } }];
incliErrorData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Inclination Error', line: { color: '#ef4444', width: 2 } }];
motorAData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Motor A', line: { color: '#22c55e', width: 2 } }];
motorBData: any[] = [{ x: [], y: [], mode: 'lines', name: 'Motor B', line: { color: '#a855f7', width: 2 } }];

// PID Tuning state
pidKp = 2.0;
pidKi = 0.0;
pidKd = 0.0;
pidSending = false;
pidMessage = '';

// Collapsible section states
inputCmdsOpen = true;
stateEstOpen = true;
```

**Subscriptions in ngOnInit:**
```typescript
// HMI Commands subscription
this.hmiSub = this.comms.hmiCommands$.subscribe((cmd: HMICommands) => {
    this.ngZone.run(() => {
        const now = new Date();
        this.pushPoint(this.bodyDirData[0].x, this.bodyDirData[0].y, now, cmd.body_direction);
        this.pushPoint(this.bodyForceData[0].x, this.bodyForceData[0].y, now, cmd.body_force);
        this.pushPoint(this.headDirData[0].x, this.headDirData[0].y, now, cmd.head_direction);
        this.pushPoint(this.headForceData[0].x, this.headForceData[0].y, now, cmd.head_force);
        // Trigger Plotly refresh...
    });
});

// State Estimation subscription
this.stateSub = this.comms.stateEstimation$.subscribe((state: StateEstimation) => {
    this.ngZone.run(() => {
        const now = new Date();
        this.pushPoint(this.pitchData[0].x, this.pitchData[0].y, now, state.current_pitch);
        this.pushPoint(this.yawData[0].x, this.yawData[0].y, now, state.current_yaw);
        this.pushPoint(this.incliErrorData[0].x, this.incliErrorData[0].y, now, state.inclination_error);
        this.pushPoint(this.motorAData[0].x, this.motorAData[0].y, now, state.motor_a_output);
        this.pushPoint(this.motorBData[0].x, this.motorBData[0].y, now, state.motor_b_output);
        // Trigger Plotly refresh...
    });
});
```

**PID Send method:**
```typescript
sendPID(): void {
    this.pidSending = true;
    this.pidMessage = '';
    this.comms.sendPIDParams({ kp: this.pidKp, ki: this.pidKi, kd: this.pidKd });
    setTimeout(() => {
        this.pidSending = false;
        this.pidMessage = 'PID parameters sent!';
    }, 500);
}
```

##### 3.5 Dashboard HTML Template

**File:** `SentryDashboard/sentry-dashboard/src/app/components/dashboard/dashboard.component.html`

**PID Tuning Card:**
```html
<div class="card pid-card">
    <div class="card-header">PID CONTROLLER TUNING</div>
    <div class="pid-grid">
        <div class="pid-field">
            <label for="kp">Kp</label>
            <input type="number" id="kp" [(ngModel)]="pidKp" step="0.1" min="0" />
        </div>
        <div class="pid-field">
            <label for="ki">Ki</label>
            <input type="number" id="ki" [(ngModel)]="pidKi" step="0.01" min="0" />
        </div>
        <div class="pid-field">
            <label for="kd">Kd</label>
            <input type="number" id="kd" [(ngModel)]="pidKd" step="0.01" min="0" />
        </div>
        <button class="pid-send-btn" (click)="sendPID()" [disabled]="pidSending">
            {{ pidSending ? 'SENDING...' : 'SEND PID' }}
        </button>
    </div>
    <div class="pid-message" *ngIf="pidMessage">{{ pidMessage }}</div>
</div>
```

**Collapsible Sections (using HTML5 `<details>`):**
```html
<!-- Input Commands Section -->
<details class="collapsible-section" [open]="inputCmdsOpen">
    <summary class="section-header" (click)="inputCmdsOpen = !inputCmdsOpen; $event.preventDefault()">
        <span class="section-title">INPUT COMMANDS TO BB-8</span>
        <span class="toggle-icon">{{ inputCmdsOpen ? '▼' : '▶' }}</span>
    </summary>
    <div class="section-content">
        <!-- Charts for body/head direction and force -->
    </div>
</details>

<!-- State Estimation Section -->
<details class="collapsible-section" [open]="stateEstOpen">
    <summary class="section-header" (click)="stateEstOpen = !stateEstOpen; $event.preventDefault()">
        <span class="section-title">BB-8 STATE ESTIMATION</span>
        <span class="toggle-icon">{{ stateEstOpen ? '▼' : '▶' }}</span>
    </summary>
    <div class="section-content">
        <!-- Charts for pitch, yaw, inclination error, motor outputs -->
    </div>
</details>
```

---

#### 4. ESP32 Firmware Modifications

##### 4.1 Global Variable Changes

**File:** `basic_control/src/main.cpp`

**Problem:** The control loop used local variables for `output_A` and `output_B`, but the telemetry function needed access to these values. Local variables shadow global ones, causing the telemetry to always report zero.

**Solution:** Made control loop outputs globally accessible:

```cpp
// Global volatile variables for cross-core access
volatile float forward_request = 0.0;
volatile float turn_request = 0.0;
volatile float inclination_goal = 0.0;
volatile float inclination_error = 0.0;
volatile float inclination_output = 0.0;
volatile float output_A = 0.0;  // Removed local declaration in controlTask()
volatile float output_B = 0.0;  // Removed local declaration in controlTask()

// PID tuning parameters (updated from serial)
float Kp = 3.0;
float Kd = 0.0;
float Ki = 0.0;
```

##### 4.2 Telemetry Transmission Function

**Rationale:** Continuous telemetry is essential for:
1. Real-time dashboard visualization
2. Debugging balance control issues
3. PID tuning feedback

```cpp
static unsigned long last_telemetry_time = 0;
const unsigned long TELEMETRY_INTERVAL = 50;  // 50ms = 20 Hz

void send_data_to_jetson()
{
    if (millis() - last_telemetry_time > TELEMETRY_INTERVAL)
    {
        last_telemetry_time = millis();
        
        // CSV format matching StateEstimation.msg fields
        Serial.printf("%f,%f,%f,%f,%f,%f,%f,%f,%f\n",
            forward_request,
            turn_request,
            inclination_goal,
            inclination_error,
            currentPitch,
            currentYaw,
            inclination_output,
            output_A,
            output_B);
    }
}
```

**Design Decisions:**

| Choice | Rationale |
|--------|-----------|
| 20 Hz rate | Balances responsiveness with serial bandwidth |
| `%f` format | Matches `float` type, parsed by Jetson's `sscanf("%f,...")` |
| CSV format | Simple parsing, minimal overhead, human-readable |
| No labels | Reduces byte count (vs. `"pitch: 1.23"`) |

##### 4.3 Serial Parsing (Enabled)

**Problem:** The serial parsing code was commented out, meaning the ESP32 never received HMI commands or PID updates from the Jetson.

**Solution:** Uncommented and verified the parsing logic:

```cpp
void loop()
{
    // Parse incoming serial commands
    if (Serial.available() > 0)
    {
        String line = Serial.readStringUntil('\n');
        
        int temp_ai;
        float temp_hd, temp_hf, temp_bd, temp_bf;
        float temp_kp, temp_ki, temp_kd;
        
        // Format: "ai_mode,head_dir,head_force,body_dir,body_force,kp,ki,kd"
        int items = sscanf(line.c_str(), "%d,%f,%f,%f,%f,%f,%f,%f",
                           &temp_ai, &temp_hd, &temp_hf, &temp_bd, &temp_bf,
                           &temp_kp, &temp_ki, &temp_kd);
        
        if (items == 8)
        {
            if (xSemaphoreTake(dataMutex, (TickType_t)10) == pdTRUE)
            {
                sharedCmd.ai_mode = (temp_ai == 1);
                sharedCmd.head_direction = temp_hd;
                sharedCmd.head_force = temp_hf;
                sharedCmd.body_direction = temp_bd;
                sharedCmd.body_force = temp_bf;
                head_direction = temp_hd;
                head_force = temp_hf;
                
                // PID values updated here!
                Kp = temp_kp;
                Ki = temp_ki;
                Kd = temp_kd;
                
                xSemaphoreGive(dataMutex);
            }
        }
    }
    
    // Send telemetry OUTSIDE the if block (runs every loop iteration)
    send_data_to_jetson();
    
    // Handle head stepper...
}
```

**Critical Bug Fixed:** The `send_data_to_jetson()` call was initially placed inside the `if (Serial.available() > 0)` block, causing telemetry to only be sent when the ESP32 received data. Moving it outside ensures continuous 20 Hz transmission.

##### 4.4 Control Loop Data Sharing

**Problem:** The `controlTask()` (running on Core 1) was using hardcoded test values instead of reading from `sharedCmd` (populated by serial parsing on Core 0).

**Solution:** Enabled mutex-protected data sharing:

```cpp
void controlTask(void *pvParameters)
{
    for (;;)
    {
        // Copy shared data with mutex protection
        if (xSemaphoreTake(dataMutex, (TickType_t)5) == pdTRUE)
        {
            ai_mode = sharedCmd.ai_mode;
            head_direction = sharedCmd.head_direction;
            head_force = sharedCmd.head_force;
            body_direction = sharedCmd.body_direction;
            body_force = sharedCmd.body_force;
            xSemaphoreGive(dataMutex);
        }
        
        // Removed test code:
        // ai_mode = true;
        // body_force = 1.0;
        // body_direction = 90.0;
        
        readIMU();
        
        if (ai_mode)
        {
            // PID control using Kp, Ki, Kd (now updatable from dashboard)
            float P_term = Kp * inclination_error;
            float I_term = Ki * accumulated_error;
            float D_term = Kd * (-currentGyroX);
            
            inclination_output = P_term + I_term + D_term;
            // ...
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
```

---

### Complete PID Tuning Data Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          DASHBOARD (Angular)                                 │
│                                                                              │
│   User enters: Kp=3.5, Ki=0.1, Kd=0.5                                       │
│   Clicks [Send PID]                                                          │
│                     │                                                        │
│                     ▼                                                        │
│   sendPID() → this.comms.sendPIDParams({kp: 3.5, ki: 0.1, kd: 0.5})        │
└─────────────────────────────────────────────────────────────────────────────┘
                      │
                      │ WebSocket (port 9091)
                      │ Message: {"op":"publish","topic":"/pid_tune","msg":{kp,ki,kd}}
                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          ROSBRIDGE (Jetson)                                  │
│                                                                              │
│   Receives WebSocket message                                                 │
│   Publishes to ROS2 topic: /pid_tune                                        │
│   Message type: bb8_cmd_receiver/msg/PIDParams                              │
└─────────────────────────────────────────────────────────────────────────────┘
                      │
                      │ ROS2 subscription callback
                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                    COMMAND RECEIVER (C++ ROS2 Node)                          │
│                                                                              │
│   pid_callback() triggered:                                                  │
│       this->kp = msg->kp;  // 3.5                                           │
│       this->ki = msg->ki;  // 0.1                                           │
│       this->kd = msg->kd;  // 0.5                                           │
│                                                                              │
│   [Waits for next HMI command...]                                           │
│                                                                              │
│   command_callback() triggered (when joystick moved):                        │
│       sprintf(buffer, "%d,%.1f,%.3f,%.1f,%.3f,%.3f,%.3f,%.3f",              │
│               ai_mode, head_dir, head_force, body_dir, body_force,          │
│               this->kp, this->ki, this->kd);                                │
│       write(serial_port_, buffer);                                          │
│                                                                              │
│   Serial output: "1,0.0,0.000,90.0,1.000,3.500,0.100,0.500\n"              │
└─────────────────────────────────────────────────────────────────────────────┘
                      │
                      │ Serial UART (115200 baud)
                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          ESP32 (Core 0 - loop())                             │
│                                                                              │
│   Serial.available() returns true                                            │
│   line = Serial.readStringUntil('\n')                                       │
│                                                                              │
│   sscanf(line, "%d,%f,%f,%f,%f,%f,%f,%f",                                   │
│          &ai, &hd, &hf, &bd, &bf, &kp, &ki, &kd);                           │
│                                                                              │
│   items == 8 → Update globals:                                              │
│       Kp = 3.5;                                                              │
│       Ki = 0.1;                                                              │
│       Kd = 0.5;                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
                      │
                      │ Shared memory (mutex protected)
                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          ESP32 (Core 1 - controlTask())                      │
│                                                                              │
│   PID Calculation (100 Hz):                                                  │
│       P_term = Kp * inclination_error;    // 3.5 × error                    │
│       I_term = Ki * accumulated_error;     // 0.1 × ∫error                  │
│       D_term = Kd * (-currentGyroX);       // 0.5 × d(error)/dt             │
│                                                                              │
│       inclination_output = P_term + I_term + D_term;                        │
│                                                                              │
│   Motor Output:                                                              │
│       output_A = turn_output - inclination_output;                          │
│       output_B = turn_output + inclination_output;                          │
│                                                                              │
│       setMotorSpeed(MCPWM_UNIT_0, output_A);                                │
│       setMotorSpeed(MCPWM_UNIT_1, output_B);                                │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

### Known Limitations and Future Work

1. **PID Update Latency:** PID values are only sent to the ESP32 when an HMI command is received. A dedicated serial message for PID-only updates would provide immediate feedback.

2. **Telemetry Rate:** The 20 Hz telemetry rate may overwhelm the serial buffer under heavy load. Consider adaptive rate limiting or compression.

3. **Error Handling:** The current implementation lacks robust error handling for:
   - Serial port disconnection
   - Malformed CSV data
   - WebSocket connection drops

4. **Parameter Persistence:** PID values are not persisted across ESP32 reboots. Consider storing in EEPROM/NVS.

---

### Testing Checklist

- [ ] Verify rosbridge connects on port 9091
- [ ] Verify `/hmi_cmds_echo` topic publishes when joystick is moved
- [ ] Verify `/robot/state_estimation` topic publishes at ~20 Hz
- [ ] Verify dashboard charts update in real-time
- [ ] Verify PID values reach ESP32 (set Kp=0, motors should not respond to tilt)
- [ ] Verify Motor A/B outputs show non-zero values in AI mode

---

### Files Modified Summary

| File | Type | Changes |
|------|------|---------|
| `ros_control/.../msg/StateEstimation.msg` | New | 9-field state estimation message |
| `ros_control/.../msg/PIDParams.msg` | New | 3-field PID parameters message |
| `ros_control/.../CMakeLists.txt` | Modified | Added new message types |
| `ros_control/.../command_receiver.hpp` | Modified | Added publishers, subscriptions, timer |
| `ros_control/.../command_receiver.cpp` | Modified | Serial read loop, echo publisher, PID callback |
| `SentryDashboard/.../environment.ts` | Modified | Added rosbridgeUrl |
| `SentryDashboard/.../models.ts` | Modified | Added 3 new interfaces |
| `SentryDashboard/.../robot-comms.service.ts` | Modified | Dual WebSocket, rosbridge protocol |
| `SentryDashboard/.../dashboard.component.ts` | Modified | Chart data, subscriptions, PID state |
| `SentryDashboard/.../dashboard.component.html` | Modified | PID card, collapsible sections |
| `SentryDashboard/.../dashboard.component.css` | Modified | Styles for new UI elements |
| `basic_control/src/main.cpp` | Modified | Telemetry, serial parsing, global vars |

---

*End of changelog entry for 2026-01-30*
