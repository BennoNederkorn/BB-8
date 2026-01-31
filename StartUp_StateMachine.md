
# BB-8 System Startup Checklist

> **Purpose**: This document specifies the automated system check routine that validates all hardware connections and communication links before normal operation. The routine is accessible via the **"System Check"** button on the Sentry Dashboard frontend.

---

## Table of Contents

1. [Overview](#1-overview)
2. [System Architecture Summary](#2-system-architecture-summary)
3. [Frontend Integration](#3-frontend-integration)
4. [Backend Service Implementation](#4-backend-service-implementation)
5. [Test Specifications](#5-test-specifications)
   - [5.1 Automated Tests (Pass/Fail)](#51-automated-tests-passfail)
   - [5.2 Manual Confirmation Tests](#52-manual-confirmation-tests)
6. [State Machine Design](#6-state-machine-design)
7. [Implementation Checklist](#7-implementation-checklist)
8. [Error Handling & Recovery](#8-error-handling--recovery)

---

## 1. Overview

### 1.1 Background

Before operating BB-8 in a "black box" (headless) configuration, all peripheral connections must be validated. This startup checklist automates the verification process through a sequence of unit tests, providing visual feedback to the operator via a modal dialogue on the Sentry Dashboard.

### 1.2 Test Characteristics

| Category | Behavior | User Interaction |
|----------|----------|------------------|
| **Automated Tests** | System executes test, parses response, determines pass/fail automatically | User observes evidence, waits 2 seconds, auto-proceeds |
| **Manual Confirmation Tests** | System triggers hardware action | User visually confirms behavior, clicks "Confirm" or "Retry" |

### 1.3 Execution Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        STARTUP CHECK STATE MACHINE                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   [IDLE] ──(Click "System Check")──> [TEST_1: Pi Connection]               │
│                                            │                                │
│                                      ┌─────┴─────┐                          │
│                                      │  PASS     │  FAIL                    │
│                                      ▼           ▼                          │
│                               [TEST_2]    [RETRY/ABORT]                     │
│                                   │                                         │
│                                   ▼                                         │
│                         ... (continue sequence) ...                         │
│                                   │                                         │
│                                   ▼                                         │
│                           [ALL_PASSED] ──> [IDLE]                           │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. System Architecture Summary

Understanding the communication pathways is critical for implementing each test.

### 2.1 Network Topology

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           TAILSCALE NETWORK                                  │
│                                                                              │
│   ┌─────────────────┐                      ┌─────────────────┐              │
│   │  Raspberry Pi   │                      │   NVIDIA Jetson │              │
│   │  (Camera Head)  │                      │   Nano (Brain)  │              │
│   │                 │    WebRTC/RTSP       │                 │              │
│   │  100.95.33.109  │ ◄──────────────────► │ 100.93.171.127  │              │
│   │                 │                      │                 │              │
│   │  - mediamtx     │                      │  - ROS2 Nodes   │              │
│   │  - ffmpeg       │                      │  - rosbridge    │              │
│   └────────┬────────┘                      │  - AI Inference │              │
│            │                               └────────┬────────┘              │
│            │                                        │                       │
│            │                               USB Serial (115200 baud)         │
│            │                                        │                       │
│            │                               ┌────────▼────────┐              │
│            │                               │     ESP32       │              │
│            │                               │  (Motor Control)│              │
│            │                               │                 │              │
│            │                               │  /dev/ttyUSB0   │              │
│            │                               │  or /dev/ttyACM0│              │
│            │                               └────────┬────────┘              │
│            │                                        │                       │
│            └────────────────────────────────────────┼───────────────────────│
│                                                     │                       │
│                            ┌────────────────────────┼────────────────────┐  │
│                            │      HARDWARE BUS      │                    │  │
│                            │                        ▼                    │  │
│                            │  ┌─────────┐  ┌─────────┐  ┌─────────────┐  │  │
│                            │  │H-Bridge │  │H-Bridge │  │   Stepper   │  │  │
│                            │  │    A    │  │    B    │  │   Driver    │  │  │
│                            │  │(Motor A)│  │(Motor B)│  │  (ULN2003)  │  │  │
│                            │  └────┬────┘  └────┬────┘  └──────┬──────┘  │  │
│                            │       │            │              │         │  │
│                            │       ▼            ▼              ▼         │  │
│                            │   DC Motor A   DC Motor B    Head Stepper   │  │
│                            │   (Left)       (Right)       (Yaw)          │  │
│                            └────────────────────────────────────────────┘  │
│                                                                              │
│   ┌─────────────────┐                                                        │
│   │ Sentry Dashboard│  WebSocket connections:                               │
│   │    (Browser)    │  - ws://100.93.171.127:9090 (rosbridge)               │
│   │                 │  - ws://100.93.171.127:9091 (inference server)        │
│   │                 │  - ws://100.93.171.127:8554/output (WebRTC video)     │
│   └─────────────────┘                                                        │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Communication Protocols

| Link | Protocol | Port/Interface | Purpose |
|------|----------|----------------|---------|
| Dashboard ↔ Rosbridge | WebSocket (rosbridge v2) | `ws://<jetson>:9090` | HMI commands, PID tuning, state estimation |
| Dashboard ↔ Inference | WebSocket (custom) | `ws://<jetson>:9091` | Telemetry, face events |
| Dashboard ↔ Video | WebRTC | `ws://<jetson>:8554/output` | Live video stream |
| Pi (ffmpeg) → Pi (mediamtx) | RTSP | `rtsp://127.0.0.1:8554/cam` | Camera capture to local RTSP server |
| Jetson ↔ Pi | RTSP/WebRTC | `rtsp://100.95.33.109:8554/cam` | Video stream relay |
| Jetson ↔ ESP32 | Serial UART | `/dev/ttyUSB0` (115200 baud) | Motor commands, sensor data |

### 2.3 ESP32 Pin Assignments (Reference from Wiring Diagram)

| Function | ESP32 Pin | Connected To |
|----------|-----------|--------------|
| Motor A L_PWM | GPIO 27 | H-Bridge A IN1 |
| Motor A R_PWM | GPIO 14 | H-Bridge A IN2 |
| Motor A L_EN | GPIO 13 | H-Bridge A EN_A |
| Motor A R_EN | GPIO 4 | H-Bridge A EN_B |
| Motor B L_PWM | GPIO 33 | H-Bridge B IN1 |
| Motor B R_PWM | GPIO 32 | H-Bridge B IN2 |
| Motor B L_EN | GPIO 26 | H-Bridge B EN_A |
| Motor B R_EN | GPIO 25 | H-Bridge B EN_B |
| Stepper Pin 1 | GPIO 19 | ULN2003 IN1 |
| Stepper Pin 2 | GPIO 18 | ULN2003 IN2 |
| Stepper Pin 3 | GPIO 5 | ULN2003 IN3 |
| Stepper Pin 4 | GPIO 17 | ULN2003 IN4 |
| I2C SDA | GPIO 21 | MPU6050 SDA |
| I2C SCL | GPIO 22 | MPU6050 SCL |

---

## 3. Frontend Integration

### 3.1 Files to Modify

| File | Modification |
|------|--------------|
| `SentryDashboard/sentry-dashboard/src/app/components/dashboard/dashboard.component.html` | Add "System Check" button |
| `SentryDashboard/sentry-dashboard/src/app/components/dashboard/dashboard.component.ts` | Add click handler, open modal |
| `SentryDashboard/sentry-dashboard/src/app/components/dashboard/dashboard.component.css` | Style the button |
| `SentryDashboard/sentry-dashboard/src/app/components/system-check-modal/` | **NEW** - Modal component for check dialogue |
| `SentryDashboard/sentry-dashboard/src/app/services/system-check.service.ts` | **NEW** - State machine logic |
| `SentryDashboard/sentry-dashboard/src/app/services/robot-comms.service.ts` | Add system check ROS topic subscriptions/publications |
| `SentryDashboard/sentry-dashboard/src/app/models.ts` | Add `SystemCheckState`, `CheckResult` interfaces |

### 3.2 Button Placement

Add the "System Check" button to the dashboard header area in `dashboard.component.html`:

```html
<!-- Suggested location: Below the PID Tuning Card or in a new "System Controls" section -->
<div class="card system-controls-card">
  <div class="card-header">SYSTEM CONTROLS</div>
  <div class="controls-grid">
    <button 
      class="system-check-btn" 
      (click)="openSystemCheckModal()"
      [disabled]="isSystemCheckRunning">
      <span class="icon">🔧</span>
      {{ isSystemCheckRunning ? 'CHECK IN PROGRESS...' : 'SYSTEM CHECK' }}
    </button>
  </div>
</div>
```

### 3.3 Modal Component Structure

Create `system-check-modal/` with these files:

```
system-check-modal/
├── system-check-modal.component.ts
├── system-check-modal.component.html
├── system-check-modal.component.css
└── index.ts
```

**Modal HTML Template** (`system-check-modal.component.html`):

```html
<div class="modal-overlay" *ngIf="isVisible" (click)="onOverlayClick($event)">
  <div class="modal-container">
    <!-- Header -->
    <div class="modal-header">
      <h2>BB-8 SYSTEM CHECK</h2>
      <span class="status-badge" [ngClass]="overallStatus">
        {{ overallStatus | uppercase }}
      </span>
    </div>

    <!-- Progress Bar -->
    <div class="progress-bar">
      <div class="progress-fill" [style.width.%]="progressPercent"></div>
    </div>
    <div class="progress-text">Test {{ currentTestIndex + 1 }} of {{ totalTests }}</div>

    <!-- Test List -->
    <div class="test-list">
      <div 
        *ngFor="let test of tests; let i = index" 
        class="test-item"
        [ngClass]="{
          'active': i === currentTestIndex,
          'passed': test.status === 'passed',
          'failed': test.status === 'failed',
          'pending': test.status === 'pending'
        }">
        
        <div class="test-icon">
          <span *ngIf="test.status === 'pending'">⏳</span>
          <span *ngIf="test.status === 'running'">🔄</span>
          <span *ngIf="test.status === 'passed'">✅</span>
          <span *ngIf="test.status === 'failed'">❌</span>
        </div>
        
        <div class="test-info">
          <div class="test-name">{{ test.name }}</div>
          <div class="test-description">{{ test.description }}</div>
        </div>
        
        <div class="test-result" *ngIf="test.status !== 'pending'">
          {{ test.resultMessage }}
        </div>
      </div>
    </div>

    <!-- Evidence Panel (for current test) -->
    <div class="evidence-panel" *ngIf="currentEvidence">
      <div class="evidence-header">DIAGNOSTIC OUTPUT</div>
      <pre class="evidence-content">{{ currentEvidence }}</pre>
    </div>

    <!-- Manual Confirmation Buttons (only for manual tests) -->
    <div class="manual-confirm-section" *ngIf="isManualTest && currentTestRequiresConfirmation">
      <p class="confirm-prompt">{{ currentTest.confirmPrompt }}</p>
      <div class="confirm-buttons">
        <button class="btn-confirm" (click)="confirmManualTest(true)">
          ✓ CONFIRMED - Behavior Observed
        </button>
        <button class="btn-retry" (click)="confirmManualTest(false)">
          ↻ RETRY - Run Test Again
        </button>
      </div>
    </div>

    <!-- Footer Actions -->
    <div class="modal-footer">
      <button class="btn-abort" (click)="abortChecks()" *ngIf="isRunning">
        ABORT
      </button>
      <button class="btn-close" (click)="closeModal()" *ngIf="!isRunning">
        CLOSE
      </button>
      <button class="btn-retry-all" (click)="restartChecks()" *ngIf="!isRunning && hasFailures">
        RETRY ALL FAILED
      </button>
    </div>
  </div>
</div>
```

### 3.4 Model Interfaces

Add to `models.ts`:

```typescript
// System Check Types
export type TestStatus = 'pending' | 'running' | 'passed' | 'failed' | 'skipped';
export type CheckCategory = 'automated' | 'manual';

export interface SystemCheckTest {
  id: string;
  name: string;
  description: string;
  category: CheckCategory;
  status: TestStatus;
  resultMessage?: string;
  evidence?: string;
  confirmPrompt?: string;  // For manual tests
  timeout: number;         // ms
}

export interface SystemCheckState {
  isRunning: boolean;
  currentTestId: string | null;
  tests: SystemCheckTest[];
  overallStatus: 'idle' | 'running' | 'passed' | 'failed';
  startTime?: Date;
  endTime?: Date;
}

// ROS message for system check commands
export interface SystemCheckCommand {
  command: 'ping_pi' | 'check_mediamtx' | 'check_esp32' | 'test_motor_a' | 'test_motor_b' | 'test_stepper' | 'read_imu';
  parameters?: any;
}

// ROS message for system check responses
export interface SystemCheckResponse {
  command: string;
  success: boolean;
  message: string;
  data?: any;
  timestamp: string;
}
```

---

## 4. Backend Service Implementation

### 4.1 Files to Create/Modify

| File | Purpose |
|------|---------|
| `ros_control/ros_ws/src/bb8_cmd_receiver/msg/SystemCheckCmd.msg` | **NEW** - ROS message for check commands |
| `ros_control/ros_ws/src/bb8_cmd_receiver/msg/SystemCheckResponse.msg` | **NEW** - ROS message for check responses |
| `ros_control/ros_ws/src/bb8_cmd_receiver/src/system_check_node.cpp` | **NEW** - Dedicated node for system checks |
| `ros_control/ros_ws/src/bb8_cmd_receiver/include/bb8_cmd_receiver/system_check_node.hpp` | **NEW** - Header file |
| `basic_control/src/main.cpp` | Add test mode command handlers |

### 4.2 ROS Message Definitions

**`SystemCheckCmd.msg`**:
```
string command          # Command identifier
string parameters       # JSON-encoded parameters (optional)
int32 timeout_ms        # Timeout in milliseconds
```

**`SystemCheckResponse.msg`**:
```
string command          # Echo of the command
bool success            # Pass/fail result
string message          # Human-readable result
string data             # JSON-encoded diagnostic data
string timestamp        # ISO 8601 timestamp
```

### 4.3 ESP32 Test Mode Protocol

The ESP32 must respond to special test commands sent via serial. Add to `main.cpp`:

```cpp
// In the serial parsing section of loop():
// New test command format: "TEST:<command>:<param>\n"
// Example: "TEST:MOTOR_A:FWD\n", "TEST:IMU:READ\n"

if (line.startsWith("TEST:")) {
    String testCmd = line.substring(5);
    handleTestCommand(testCmd);
}

void handleTestCommand(String cmd) {
    if (cmd.startsWith("MOTOR_A:")) {
        testMotorA(cmd.substring(8));
    } else if (cmd.startsWith("MOTOR_B:")) {
        testMotorB(cmd.substring(8));
    } else if (cmd.startsWith("STEPPER:")) {
        testStepper(cmd.substring(8));
    } else if (cmd.startsWith("IMU:READ")) {
        readAndReportIMU();
    } else if (cmd == "PING") {
        Serial.println("TEST_RESPONSE:PONG");
    }
}
```

---

## 5. Test Specifications

### 5.1 Automated Tests (Pass/Fail)

---

#### TEST 1: Raspberry Pi Connection (Tailscale Network)

| Property | Value |
|----------|-------|
| **Test ID** | `PI_PING` |
| **Category** | Automated |
| **Timeout** | 5000 ms |
| **Retry Count** | 3 |

**Purpose**: Verify the Raspberry Pi camera head unit is reachable over the Tailscale VPN network.

**Execution Method**:
1. Dashboard sends `SystemCheckCmd` with `command: "ping_pi"` to rosbridge topic `/system_check/command`
2. The system check ROS node on Jetson executes: `ping -c 3 -W 1 100.95.33.109`
3. Parse ping output for packet loss percentage

**Evidence Collected**:
```
PING 100.95.33.109 (100.95.33.109) 56(84) bytes of data.
64 bytes from 100.95.33.109: icmp_seq=1 ttl=64 time=1.23 ms
64 bytes from 100.95.33.109: icmp_seq=2 ttl=64 time=0.98 ms
64 bytes from 100.95.33.109: icmp_seq=3 ttl=64 time=1.05 ms

--- 100.95.33.109 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2003ms
rtt min/avg/max/mdev = 0.980/1.086/1.230/0.103 ms
```

**Pass Criteria**:
- ✅ **PASS**: Packet loss = 0%, average RTT < 50ms
- ⚠️ **WARN**: Packet loss < 33%, average RTT < 100ms
- ❌ **FAIL**: Packet loss ≥ 33% OR timeout OR host unreachable

**Why This Test Matters**:
- The Pi hosts the camera stream via mediamtx
- Without Pi connectivity, no video feed is possible
- High latency indicates network congestion that will affect video quality

---

#### TEST 2: MediaMTX Video Server Status

| Property | Value |
|----------|-------|
| **Test ID** | `MEDIAMTX_STATUS` |
| **Category** | Automated |
| **Timeout** | 8000 ms |
| **Depends On** | `PI_PING` |

**Purpose**: Verify mediamtx and ffmpeg are running on the Pi, actively capturing from the camera and publishing video streams.

**Background - Pi Video Pipeline**:

The Raspberry Pi runs two processes as systemd services (auto-start on boot):

```bash
# Service 1: mediamtx (RTSP/WebRTC server)
./mediamtx

# Service 2: ffmpeg (camera capture → RTSP push)
ffmpeg -f v4l2 -framerate 15 -video_size 320x240 -i /dev/video0 \
  -vf format=yuv420p \
  -vcodec libx264 -profile:v baseline -preset ultrafast -tune zerolatency -b:v 400k \
  -x264-params "repeat-headers=1:scenecut=0:keyint=30" \
  -f rtsp rtsp://127.0.0.1:8554/cam
```

**ffmpeg Parameters Explained**:
| Parameter | Purpose |
|-----------|----------|
| `-f v4l2` | Use Video4Linux2 input (USB camera) |
| `-framerate 15` | 15 FPS (reduced for Pi 3 CPU) |
| `-video_size 320x240` | Low resolution for bandwidth |
| `-vcodec libx264` | H.264 encoding |
| `-preset ultrafast` | Minimize encoding latency |
| `-tune zerolatency` | Disable frame buffering |
| `-b:v 400k` | 400 kbps bitrate |
| `-f rtsp` | Output to RTSP server |

**Execution Method**:
1. The system check node on Jetson makes an HTTP request to the Pi's mediamtx API
2. URL: `http://100.95.33.109:9997/v3/paths/list`
3. Parse JSON response for the `/cam` stream path

**Evidence Collected**:
```json
{
  "items": [
    {
      "name": "cam",
      "confName": "cam",
      "source": {
        "type": "rtspSession"
      },
      "ready": true,
      "readyTime": "2026-01-31T10:30:00Z",
      "tracks": ["H264"]
    }
  ]
}
```

**Alternative Check** (if API unavailable):
```bash
# Check mediamtx service
systemctl status mediamtx

# Check ffmpeg service
systemctl status ffmpeg-camera

# Test RTSP stream directly
ffprobe rtsp://100.95.33.109:8554/cam
```

**Pass Criteria**:
- ✅ **PASS**: Path `/cam` exists with `ready: true` AND `source.type: "rtspSession"` AND contains H264 track
- ⚠️ **WARN**: mediamtx running but `/cam` path not ready (ffmpeg may have crashed)
- ❌ **FAIL**: HTTP connection refused (mediamtx not running) OR no paths available

**Why This Test Matters**:
- mediamtx is the RTSP/WebRTC server that makes the video stream accessible
- ffmpeg captures from `/dev/video0` and pushes to mediamtx
- Both services must be running for video to reach the dashboard
- The `rtspSession` source type confirms ffmpeg is actively pushing frames

**Troubleshooting Hints** (shown on failure):
- If mediamtx not running: `sudo systemctl start mediamtx`
- If ffmpeg not running: `sudo systemctl start ffmpeg-camera`
- If camera not detected: Check `ls /dev/video*` and USB connection
- If ffmpeg crashes repeatedly: Check CPU temperature (`vcgencmd measure_temp`)

---

#### TEST 3: ESP32 Serial Connection

| Property | Value |
|----------|-------|
| **Test ID** | `ESP32_SERIAL` |
| **Category** | Automated |
| **Timeout** | 3000 ms |
| **Retry Count** | 3 |

**Purpose**: Verify the ESP32 motor controller is connected via USB serial and responding.

**Execution Method**:
1. The ROS `command_receiver` node checks if serial port is open
2. Send test ping command: `TEST:PING\n` over serial
3. Wait for response: `TEST_RESPONSE:PONG\n`
4. Measure round-trip time

**Evidence Collected**:
```
Serial Port: /dev/ttyUSB0
Baud Rate: 115200
TX: TEST:PING
RX: TEST_RESPONSE:PONG
Round-trip: 12 ms
```

**Pass Criteria**:
- ✅ **PASS**: Response received within timeout, RTT < 100ms
- ❌ **FAIL**: No response OR serial port not found (`/dev/ttyUSB0`, `/dev/ttyUSB1`, `/dev/ttyACM0` all fail)

**Why This Test Matters**:
- ESP32 is the sole actuator controller
- No ESP32 = no motor control = robot is immobile
- Serial connectivity issues can cause command latency or data corruption

**Troubleshooting Hints** (shown on failure):
- Check USB cable connection
- Run `dmesg | tail -20` to see if device was detected
- Verify Docker has `/dev:/dev` volume mount
- Check if another process has the serial port open

---

### 5.2 Manual Confirmation Tests

These tests require the operator to **visually observe** the robot's physical behavior and confirm via the UI.

---

#### TEST 4: H-Bridge Driver A (Motor A / Left DC Motor)

| Property | Value |
|----------|-------|
| **Test ID** | `MOTOR_A_TEST` |
| **Category** | Manual Confirmation |
| **Timeout** | 15000 ms (user interaction) |
| **Depends On** | `ESP32_SERIAL` |

**Purpose**: Verify H-Bridge A and DC Motor A (left wheel) are functional.

**Execution Method**:
1. Dashboard sends `SystemCheckCmd` with `command: "test_motor_a"`
2. ROS node forwards to ESP32: `TEST:MOTOR_A:SEQUENCE\n`
3. ESP32 executes test sequence:
   ```cpp
   void testMotorA(String param) {
       Serial.println("TEST_RESPONSE:MOTOR_A:STARTING");
       
       // Forward pulse (50% duty, 1 second)
       setMotorSpeed(MCPWM_UNIT_0, 50.0);
       delay(1000);
       
       // Stop (0.5 seconds)
       setMotorSpeed(MCPWM_UNIT_0, 0.0);
       delay(500);
       
       // Reverse pulse (50% duty, 1 second)
       setMotorSpeed(MCPWM_UNIT_0, -50.0);
       delay(1000);
       
       // Stop
       setMotorSpeed(MCPWM_UNIT_0, 0.0);
       
       Serial.println("TEST_RESPONSE:MOTOR_A:COMPLETE");
   }
   ```
4. User observes robot behavior
5. Modal shows confirmation prompt

**Expected Physical Behavior**:
- Motor A spins **forward** for 1 second (robot would move forward-left if only this motor active)
- 0.5 second pause
- Motor A spins **reverse** for 1 second
- Motor stops

**Confirmation Prompt**:
> "Did Motor A (LEFT wheel) spin forward for 1 second, pause, then spin backward for 1 second?"

**Evidence Displayed**:
```
Motor A Test Sequence:
├─ [0.0s] Forward 50% PWM → GPIO 27 HIGH, GPIO 14 LOW
├─ [1.0s] Stop
├─ [1.5s] Reverse 50% PWM → GPIO 27 LOW, GPIO 14 HIGH
└─ [2.5s] Stop
ESP32 Response: TEST_RESPONSE:MOTOR_A:COMPLETE
```

**Pass Criteria**:
- ✅ **PASS**: User clicks "Confirmed - Behavior Observed"
- ❌ **FAIL**: User clicks "Retry" 3 times OR timeout

**Why This Test Matters**:
- Verifies GPIO → H-Bridge → Motor wiring is correct
- Confirms H-Bridge EN pins (GPIO 13, 4) are enabling the driver
- Checks for motor driver overheating or fault conditions
- Forward/reverse test catches wiring polarity issues

---

#### TEST 5: H-Bridge Driver B (Motor B / Right DC Motor)

| Property | Value |
|----------|-------|
| **Test ID** | `MOTOR_B_TEST` |
| **Category** | Manual Confirmation |
| **Timeout** | 15000 ms |
| **Depends On** | `ESP32_SERIAL` |

**Purpose**: Verify H-Bridge B and DC Motor B (right wheel) are functional.

**Execution Method**:
Identical to TEST 4, but targeting Motor B:
- Uses `MCPWM_UNIT_1` 
- GPIO 33 (L_PWM), GPIO 32 (R_PWM)
- Enable pins: GPIO 26, GPIO 25

**Expected Physical Behavior**:
- Motor B spins **forward** for 1 second
- 0.5 second pause
- Motor B spins **reverse** for 1 second
- Motor stops

**Confirmation Prompt**:
> "Did Motor B (RIGHT wheel) spin forward for 1 second, pause, then spin backward for 1 second?"

**Evidence Displayed**:
```
Motor B Test Sequence:
├─ [0.0s] Forward 50% PWM → GPIO 33 HIGH, GPIO 32 LOW
├─ [1.0s] Stop
├─ [1.5s] Reverse 50% PWM → GPIO 33 LOW, GPIO 32 HIGH
└─ [2.5s] Stop
ESP32 Response: TEST_RESPONSE:MOTOR_B:COMPLETE
```

**Pass Criteria**:
- ✅ **PASS**: User clicks "Confirmed"
- ❌ **FAIL**: User clicks "Retry" 3 times OR timeout

---

#### TEST 6: Stepper Motor Driver (Head Yaw)

| Property | Value |
|----------|-------|
| **Test ID** | `STEPPER_TEST` |
| **Category** | Manual Confirmation |
| **Timeout** | 20000 ms |
| **Depends On** | `ESP32_SERIAL` |

**Purpose**: Verify ULN2003 stepper driver and head yaw motor are functional.

**Execution Method**:
1. Dashboard sends `command: "test_stepper"`
2. ESP32 executes test sequence:
   ```cpp
   void testStepper(String param) {
       Serial.println("TEST_RESPONSE:STEPPER:STARTING");
       
       // Clockwise rotation (200 steps at 400 steps/sec)
       headStepper.setSpeed(400);
       for (int i = 0; i < 200; i++) {
           headStepper.runSpeed();
           delay(2);
       }
       
       delay(500); // Pause
       
       // Counter-clockwise rotation (200 steps)
       headStepper.setSpeed(-400);
       for (int i = 0; i < 200; i++) {
           headStepper.runSpeed();
           delay(2);
       }
       
       Serial.println("TEST_RESPONSE:STEPPER:COMPLETE");
   }
   ```

**Expected Physical Behavior**:
- BB-8's head rotates **clockwise** approximately 17° (200 steps / 4096 steps per revolution × 360°)
- 0.5 second pause
- Head rotates **counter-clockwise** returning to original position

**Confirmation Prompt**:
> "Did BB-8's head rotate clockwise, pause, then rotate counter-clockwise back to the starting position?"

**Evidence Displayed**:
```
Stepper Test Sequence:
├─ [0.0s] Clockwise 200 steps (≈17°) @ 400 steps/sec
│         Step pins: GPIO 19→18→5→17 sequence
├─ [0.5s] Pause
├─ [1.0s] Counter-clockwise 200 steps @ 400 steps/sec
│         Step pins: GPIO 17→5→18→19 sequence
└─ [1.5s] Complete
ESP32 Response: TEST_RESPONSE:STEPPER:COMPLETE
```

**Pass Criteria**:
- ✅ **PASS**: User confirms head moved in both directions
- ❌ **FAIL**: Head didn't move OR moved erratically OR overheated

**Why This Test Matters**:
- Stepper wiring order (Pin1→Pin3→Pin2→Pin4 in code) must match physical coil sequence
- ULN2003 can overheat if coils energized too long
- Head position tracking relies on step counting (no encoder feedback)

---

#### TEST 7: IMU Sensor (MPU6050)

| Property | Value |
|----------|-------|
| **Test ID** | `IMU_TEST` |
| **Category** | Manual Confirmation |
| **Timeout** | 10000 ms |
| **Depends On** | `ESP32_SERIAL` |

**Purpose**: Verify MPU6050 IMU is connected and returning valid sensor data.

**Execution Method**:
1. Dashboard sends `command: "read_imu"`
2. ESP32 reads sensor and streams data for 3 seconds:
   ```cpp
   void readAndReportIMU() {
       Serial.println("TEST_RESPONSE:IMU:STARTING");
       
       for (int i = 0; i < 30; i++) {  // 30 samples over 3 seconds
           readIMU();  // Uses existing complementary filter
           
           Serial.printf("TEST_RESPONSE:IMU:DATA:%.2f,%.2f,%.2f,%.3f,%.3f,%.3f\n",
               currentPitch, currentRoll, currentYaw,
               currentGyroX, currentGyroY, currentGyroZ);
           
           delay(100);
       }
       
       Serial.println("TEST_RESPONSE:IMU:COMPLETE");
   }
   ```
3. Dashboard displays real-time sensor values in modal
4. User physically tilts/rotates the robot to verify responsiveness

**Evidence Displayed** (live updating):
```
MPU6050 IMU Readings (Live):
┌──────────────────────────────────────────────────────────┐
│  ORIENTATION (Complementary Filter)                      │
│  ─────────────────────────────────────────────────       │
│  Pitch:  -2.34°    [████████████████░░░░░░░] -20° to 20° │
│  Roll:    0.87°    [██████████████████░░░░░] -20° to 20° │
│  Yaw:    45.12°    [Accumulating, no absolute reference] │
│                                                          │
│  ANGULAR VELOCITY (Gyroscope)                            │
│  ─────────────────────────────────────────────────       │
│  GyroX:  0.012 rad/s                                     │
│  GyroY: -0.008 rad/s                                     │
│  GyroZ:  0.003 rad/s                                     │
│                                                          │
│  I2C Address: 0x68                                       │
│  Sample Rate: 10 Hz (test mode)                          │
└──────────────────────────────────────────────────────────┘

⟳ Tilt or rotate the robot to verify sensor responsiveness
```

**Confirmation Prompt**:
> "Do the pitch and roll values change when you tilt the robot? Do gyroscope values respond to rotation?"

**Pass Criteria**:
- ✅ **PASS**: User confirms sensor values update responsively
- ❌ **FAIL**: 
  - Values stuck at 0 (I2C communication failure)
  - Values stuck at constant non-zero (sensor not updating)
  - Wild/nonsensical values (sensor fault)
  - I2C timeout error

**Why This Test Matters**:
- IMU is critical for balance control (PID loop uses pitch for inclination)
- I2C wiring issues (SDA/SCL swapped, missing pull-ups) cause hangs
- Gyro bias calibration values in code must match physical sensor
- Complementary filter relies on both accelerometer AND gyroscope

---

## 6. State Machine Design

### 6.1 States

```typescript
enum CheckState {
  IDLE = 'idle',
  RUNNING = 'running',
  WAITING_CONFIRMATION = 'waiting_confirmation',
  TEST_PASSED = 'test_passed',
  TEST_FAILED = 'test_failed',
  ALL_COMPLETE = 'all_complete',
  ABORTED = 'aborted'
}
```

### 6.2 State Transitions

```
                                    ┌──────────────────┐
                                    │                  │
                                    ▼                  │
┌──────┐  startChecks()   ┌─────────────────┐         │
│ IDLE │ ───────────────► │ RUNNING         │         │
└──────┘                  │ (Test N)        │         │
    ▲                     └────────┬────────┘         │
    │                              │                  │
    │                   ┌──────────┼──────────┐       │
    │                   │          │          │       │
    │              (automated)  (manual)   (timeout)  │
    │                   │          │          │       │
    │                   ▼          ▼          ▼       │
    │             ┌─────────┐ ┌────────────┐ ┌──────┐ │
    │             │ PASS/   │ │ WAITING    │ │ FAIL │ │
    │             │ FAIL    │ │ CONFIRM    │ │      │ │
    │             │ (auto)  │ │            │ │      │ │
    │             └────┬────┘ └─────┬──────┘ └──┬───┘ │
    │                  │            │           │     │
    │                  │     user   │           │     │
    │                  │   confirm  │           │     │
    │                  │     ┌──────┴──────┐    │     │
    │                  │     │             │    │     │
    │                  │  confirm()     retry() │     │
    │                  │     │             │    │     │
    │                  │     ▼             │    │     │
    │                  │  ┌──────┐         │    │     │
    │                  │  │ PASS │         │    │     │
    │                  │  └──┬───┘         │    │     │
    │                  │     │             │    │     │
    │                  └─────┴──────┬──────┴────┘     │
    │                               │                 │
    │                       hasNextTest?              │
    │                      ┌────────┴────────┐        │
    │                      │                 │        │
    │                    yes                no        │
    │                      │                 │        │
    │                      │      ┌──────────▼───────┐│
    │                      │      │ ALL_COMPLETE     ││
    │                      │      └──────────┬───────┘│
    │                      │                 │        │
    │                      └────────►────────┴────────┘
    │                            nextTest()
    │                                │
    │                                ▼
    │ ◄────────────────────── (back to RUNNING)
    │
    │                     ┌───────────┐
    └─────────────────────┤ closeModal│
        abortChecks()     └───────────┘
```

### 6.3 Service Implementation (`system-check.service.ts`)

```typescript
import { Injectable } from '@angular/core';
import { BehaviorSubject, Observable, Subject, timer, takeUntil, firstValueFrom } from 'rxjs';
import { RobotCommsService } from './robot-comms.service';
import { SystemCheckTest, SystemCheckState, SystemCheckResponse } from '../models';

@Injectable({ providedIn: 'root' })
export class SystemCheckService {
  private stateSubject = new BehaviorSubject<SystemCheckState>({
    isRunning: false,
    currentTestId: null,
    tests: this.initializeTests(),
    overallStatus: 'idle'
  });
  
  public state$ = this.stateSubject.asObservable();
  private abortSignal = new Subject<void>();

  constructor(private comms: RobotCommsService) {}

  private initializeTests(): SystemCheckTest[] {
    return [
      {
        id: 'PI_PING',
        name: 'Raspberry Pi Connection',
        description: 'Ping camera unit over Tailscale (100.95.33.109)',
        category: 'automated',
        status: 'pending',
        timeout: 5000
      },
      {
        id: 'MEDIAMTX_STATUS',
        name: 'Video Server Status',
        description: 'Check mediamtx is streaming on Pi',
        category: 'automated',
        status: 'pending',
        timeout: 8000
      },
      {
        id: 'ESP32_SERIAL',
        name: 'ESP32 Connection',
        description: 'Verify serial link to motor controller',
        category: 'automated',
        status: 'pending',
        timeout: 3000
      },
      {
        id: 'MOTOR_A_TEST',
        name: 'Motor A (Left)',
        description: 'Test H-Bridge A and DC motor forward/reverse',
        category: 'manual',
        status: 'pending',
        confirmPrompt: 'Did Motor A (LEFT wheel) spin forward for 1s, pause, then reverse for 1s?',
        timeout: 15000
      },
      {
        id: 'MOTOR_B_TEST',
        name: 'Motor B (Right)',
        description: 'Test H-Bridge B and DC motor forward/reverse',
        category: 'manual',
        status: 'pending',
        confirmPrompt: 'Did Motor B (RIGHT wheel) spin forward for 1s, pause, then reverse for 1s?',
        timeout: 15000
      },
      {
        id: 'STEPPER_TEST',
        name: 'Head Stepper',
        description: 'Test ULN2003 driver clockwise/counter-clockwise',
        category: 'manual',
        status: 'pending',
        confirmPrompt: 'Did the head rotate clockwise, pause, then counter-clockwise?',
        timeout: 20000
      },
      {
        id: 'IMU_TEST',
        name: 'IMU Sensor',
        description: 'Read MPU6050 pitch/roll/gyro data',
        category: 'manual',
        status: 'pending',
        confirmPrompt: 'Do pitch/roll values change when tilting the robot?',
        timeout: 10000
      }
    ];
  }

  async startChecks(): Promise<void> {
    // Reset state
    const tests = this.initializeTests();
    this.stateSubject.next({
      isRunning: true,
      currentTestId: tests[0].id,
      tests: tests,
      overallStatus: 'running',
      startTime: new Date()
    });

    // Run tests sequentially
    for (const test of tests) {
      const result = await this.runTest(test);
      if (!result) break; // Aborted
    }
  }

  // ... additional methods for runTest, confirmManualTest, abort, etc.
}
```

---

## 7. Implementation Checklist

### 7.1 Frontend Tasks

- [ ] **Create `SystemCheckService`** (`src/app/services/system-check.service.ts`)
  - State machine logic
  - ROS topic communication
  - Test sequencing
  
- [ ] **Create Modal Component** (`src/app/components/system-check-modal/`)
  - Modal container with overlay
  - Progress bar
  - Test list with status icons
  - Evidence panel
  - Manual confirmation buttons
  
- [ ] **Update `DashboardComponent`**
  - Add "System Check" button
  - Import and display modal
  - Wire up click handler
  
- [ ] **Update `models.ts`**
  - Add `SystemCheckTest`, `SystemCheckState`, etc.
  
- [ ] **Update `robot-comms.service.ts`**
  - Add `sendSystemCheckCommand()` method
  - Add `systemCheckResponse$` observable
  - Subscribe to `/system_check/response` topic

- [ ] **Styling**
  - Modal animations (fade in/out)
  - Test status animations (spinner, checkmark, X)
  - Progress bar styling
  - Evidence panel syntax highlighting

### 7.2 Backend Tasks (ROS/Jetson)

- [ ] **Create ROS Messages**
  - `SystemCheckCmd.msg`
  - `SystemCheckResponse.msg`
  
- [ ] **Create System Check Node**
  - Subscribe to `/system_check/command`
  - Publish to `/system_check/response`
  - Implement Pi ping logic
  - Implement mediamtx HTTP check
  - Forward ESP32 test commands
  
- [ ] **Update `command_receiver.cpp`**
  - Handle system check commands
  - Route test commands to ESP32
  - Parse test responses

- [ ] **Update rosbridge configuration**
  - Ensure `/system_check/*` topics are advertised

### 7.3 Firmware Tasks (ESP32)

- [ ] **Update `main.cpp`**
  - Add `handleTestCommand()` function
  - Add `testMotorA()`, `testMotorB()` functions
  - Add `testStepper()` function
  - Add `readAndReportIMU()` function
  - Handle `TEST:PING` for connectivity check

### 7.4 Testing & Verification

- [ ] Test each check individually via ROS CLI
- [ ] Test full sequence with mock failures
- [ ] Test retry logic
- [ ] Test abort functionality
- [ ] Test timeout handling
- [ ] Verify evidence collection accuracy

---

## 8. Error Handling & Recovery

### 8.1 Common Failure Scenarios

| Failure | Detection | Recovery Action |
|---------|-----------|-----------------|
| Pi unreachable | Ping timeout | Check Tailscale status, restart tailscaled |
| mediamtx not running | HTTP 503/timeout | SSH to Pi, run `sudo systemctl start mediamtx` |
| ffmpeg not running | mediamtx shows no `/cam` path | SSH to Pi, run `sudo systemctl start ffmpeg-camera` |
| Camera not detected | ffmpeg service fails repeatedly | Check `ls /dev/video*`, reconnect USB camera |
| ESP32 not responding | Serial timeout | Check USB connection, reset ESP32 |
| Motor doesn't move | User reports no movement | Check 12V power supply, check H-Bridge wiring |
| Stepper vibrates but doesn't rotate | User reports buzzing | Check coil sequence wiring, reduce speed |
| IMU returns zeros | All values = 0 | Check I2C wiring, pull-up resistors |
| IMU hangs system | I2C timeout | Reboot ESP32, check for short circuits |

### 8.2 Error Messages

Each failure should display:
1. **What failed** (clear description)
2. **Why it might have failed** (common causes)
3. **How to fix it** (actionable steps)

Example:
```
❌ TEST FAILED: ESP32 Serial Connection

REASON: No response received within 3000ms timeout.

COMMON CAUSES:
• USB cable disconnected or faulty
• ESP32 not powered on
• Serial port claimed by another process
• Wrong baud rate (expected: 115200)

TROUBLESHOOTING STEPS:
1. Check the USB cable between Jetson and ESP32
2. Verify ESP32 LED is blinking (indicates powered)
3. Run: ls /dev/ttyUSB* /dev/ttyACM* 
4. Run: dmesg | tail -20 | grep -i usb
5. Restart the ros_control Docker container

[RETRY]  [SKIP]  [ABORT]
```

---

## Appendix A: Wire Reference

| Connection | From | To | Wire Color (suggested) |
|------------|------|----|-----------------------|
| 11.1V Battery | XT60 | 5V Buck Converter | Red (+), Black (-) |
| 14.8V Battery | XT60 | 12V Switch | Red (+), Black (-) |
| 12V Rail | Buck Out | H-Bridge VCC | Red |
| 5V Rail | Buck Out | ESP32 VIN | Red |
| GND | Common | All | Black |
| Motor A PWM1 | ESP32 GPIO27 | H-Bridge A IN1 | Yellow |
| Motor A PWM2 | ESP32 GPIO14 | H-Bridge A IN2 | Orange |
| Motor B PWM3 | ESP32 GPIO33 | H-Bridge B IN1 | Yellow |
| Motor B PWM4 | ESP32 GPIO32 | H-Bridge B IN2 | Orange |
| Stepper | ESP32 GPIO19,18,5,17 | ULN2003 IN1-4 | Blue |
| I2C | ESP32 GPIO21,22 | MPU6050 SDA,SCL | Green, White |

---

## Appendix B: Test Command Reference

| Command String | Direction | Description |
|----------------|-----------|-------------|
| `TEST:PING` | Jetson → ESP32 | Connectivity check |
| `TEST:MOTOR_A:SEQUENCE` | Jetson → ESP32 | Run Motor A test |
| `TEST:MOTOR_B:SEQUENCE` | Jetson → ESP32 | Run Motor B test |
| `TEST:STEPPER:SEQUENCE` | Jetson → ESP32 | Run stepper test |
| `TEST:IMU:READ` | Jetson → ESP32 | Stream IMU data |
| `TEST_RESPONSE:PONG` | ESP32 → Jetson | Ping acknowledgment |
| `TEST_RESPONSE:MOTOR_A:COMPLETE` | ESP32 → Jetson | Motor A test done |
| `TEST_RESPONSE:MOTOR_B:COMPLETE` | ESP32 → Jetson | Motor B test done |
| `TEST_RESPONSE:STEPPER:COMPLETE` | ESP32 → Jetson | Stepper test done |
| `TEST_RESPONSE:IMU:DATA:p,r,y,gx,gy,gz` | ESP32 → Jetson | IMU sample |
| `TEST_RESPONSE:IMU:COMPLETE` | ESP32 → Jetson | IMU streaming done |

---

## Appendix C: Raspberry Pi Video Streaming Services

The Raspberry Pi (camera head unit) runs two systemd services that auto-start on boot to provide the video stream.

### C.1 Service Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         RASPBERRY PI (100.95.33.109)                     │
│                                                                          │
│   ┌─────────────────┐         RTSP          ┌─────────────────┐         │
│   │     ffmpeg      │ ──────────────────────►│    mediamtx     │         │
│   │  (Camera Input) │   rtsp://127.0.0.1:    │  (RTSP Server)  │         │
│   │                 │      8554/cam          │                 │         │
│   └────────┬────────┘                        └────────┬────────┘         │
│            │                                          │                  │
│            │ /dev/video0                              │ Port 8554        │
│            │ (USB Camera)                             │ (RTSP/WebRTC)    │
│            ▼                                          ▼                  │
│   ┌─────────────────┐                        ┌─────────────────┐         │
│   │   USB Webcam    │                        │   External      │         │
│   │   (320x240)     │                        │   Clients       │         │
│   │   (15 FPS)      │                        │   (Jetson, etc) │         │
│   └─────────────────┘                        └─────────────────┘         │
└─────────────────────────────────────────────────────────────────────────┘
```

### C.2 Service 1: mediamtx (RTSP/WebRTC Server)

**Service File**: `/etc/systemd/system/mediamtx.service`

```ini
[Unit]
Description=MediaMTX RTSP/WebRTC Server
After=network.target

[Service]
Type=simple
WorkingDirectory=/home/pi/mediamtx
ExecStart=/home/pi/mediamtx/mediamtx
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

**Manual Commands**:
```bash
# Start service
sudo systemctl start mediamtx

# Stop service
sudo systemctl stop mediamtx

# Check status
sudo systemctl status mediamtx

# View logs
journalctl -u mediamtx -f
```

### C.3 Service 2: ffmpeg-camera (Camera Capture)

**Service File**: `/etc/systemd/system/ffmpeg-camera.service`

```ini
[Unit]
Description=FFmpeg Camera Stream to MediaMTX
After=mediamtx.service
Requires=mediamtx.service

[Service]
Type=simple
ExecStart=/usr/bin/ffmpeg \
  -f v4l2 \
  -framerate 15 \
  -video_size 320x240 \
  -i /dev/video0 \
  -vf format=yuv420p \
  -vcodec libx264 \
  -profile:v baseline \
  -preset ultrafast \
  -tune zerolatency \
  -b:v 400k \
  -x264-params "repeat-headers=1:scenecut=0:keyint=30" \
  -f rtsp rtsp://127.0.0.1:8554/cam
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

**Manual Commands**:
```bash
# Start service
sudo systemctl start ffmpeg-camera

# Stop service  
sudo systemctl stop ffmpeg-camera

# Check status
sudo systemctl status ffmpeg-camera

# View logs
journalctl -u ffmpeg-camera -f
```

### C.4 ffmpeg Parameters Reference

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `-f v4l2` | - | Use Video4Linux2 input driver for Linux USB cameras |
| `-framerate 15` | 15 FPS | Reduced framerate to minimize CPU load on Pi 3 |
| `-video_size 320x240` | QVGA | Low resolution for reduced bandwidth (~400kbps) |
| `-i /dev/video0` | - | Input device (first USB camera) |
| `-vf format=yuv420p` | - | Force YUV420 pixel format (required for H.264) |
| `-vcodec libx264` | - | H.264 software encoder |
| `-profile:v baseline` | - | Most compatible H.264 profile |
| `-preset ultrafast` | - | Fastest encoding (lowest CPU, lowest quality) |
| `-tune zerolatency` | - | Disable frame buffering for real-time streaming |
| `-b:v 400k` | 400 kbps | Target video bitrate |
| `-x264-params` | `repeat-headers=1` | Include SPS/PPS with each keyframe |
| | `scenecut=0` | Disable scene change detection |
| | `keyint=30` | Keyframe every 30 frames (2 seconds at 15fps) |
| `-f rtsp` | - | Output format: RTSP stream |
| `rtsp://127.0.0.1:8554/cam` | - | Local mediamtx server, path `/cam` |

### C.5 Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| No video, ffmpeg exits immediately | Camera not connected | Check `ls /dev/video*`, reconnect USB camera |
| High latency (>500ms) | Network congestion | Check Tailscale ping times, reduce bitrate |
| Video freezes periodically | Pi CPU throttling | Check `vcgencmd measure_temp`, add cooling |
| "Address already in use" | Port conflict | Stop mediamtx, wait 30s, restart |
| mediamtx shows no streams | ffmpeg not running | Start ffmpeg-camera service |

---

*Document Version: 1.0*  
*Last Updated: 2026-01-31*  
*Author: System Integration Team*