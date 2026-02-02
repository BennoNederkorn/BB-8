# DIY BB-8 Project

<img src="https://cdn.pixabay.com/photo/2017/07/31/16/41/bb8-2558879_1280.jpg" alt="drawing" width="400"/>

A repository documenting the build of a personal, remote-controlled BB-8 droid inspired by _Star Wars_. This project uses common hobbyist electronics and a mix of 3D-printed and custom-made parts to recreate the iconic droid's movement.

---

## 🌟 Features

- **Spherical Movement:** Rolls in any direction using a "hamster-style" internal drive mechanism.
- **Magnetic Head:** The head "floats" on top of the body, held in place by magnets, and can look around.
- **Remote Control:** Controlled via a custom smartphone app or web interface using Bluetooth/Wi-Fi.
- **Sound Effects:** Includes a small speaker to play BB-8's iconic beeps and boops.

---

## 🛠️ Materials & Components

### Electronics

- **Main Controller:** Arduino Uno (or Raspberry Pi for more advanced features)
- **Motor Controller:** L298N Dual H-Bridge Motor Driver
- **Drive Motors:** 2x 12V DC Geared Motors with Wheels
- **Head Control:** 1x SG90 Micro Servo (for head "look" mechanism)
- **Wireless:** HC-05 Bluetooth Module (for Arduino) or built-in Wi-Fi (for Raspberry Pi)
- **Power:** 11.1V 3S LiPo Battery (or a suitable 12V battery pack)
- **Sound:** PAM8403 Audio Amplifier Module & a small 3W Speaker
- **Misc:** Wires, switches, buck converter (to step down voltage for Arduino/servo)

### Hardware & Body

- **Main Body:** Large (e.g., 16-20 inch) hollow plastic sphere or 3D-printed sphere. A large hamster ball or acrylic globe can also work.
- **Head:** 3D-printed head (models available on Thingiverse) or a styrofoam ball, shaped and detailed.
- **Internal Chassis:** 3D-printed or custom-built frame (from wood or acrylic) to hold all electronics, motors, and battery.
- **Magnets:** Strong neodymium magnets (e.g., N52) for coupling the head to the internal drive.
- **Paint:** White, orange, and silver spray paint (or acrylics) for detailing.

---

## 🏗️ Build Process (Overview)

1.  **The Body:** The sphere was either 3D-printed in sections and assembled or based on a pre-existing acrylic globe. It was then sanded, primed, and painted.
2.  **The Internal Drive:** A pendulum-style chassis was designed and 3D-printed. This chassis holds the motors, controller, and battery low to create a center of gravity. As the wheels turn, they drive against the inner wall of the sphere, causing the whole ball to roll.
3.  **The Head:** The head was 3D-printed, painted, and detailed. A set of magnets was embedded in its base. A corresponding magnet assembly, controlled by a servo, is mounted on a mast at the top of the internal chassis, just under the sphere's inner "roof."
4.  **Assembly & Wiring:** All electronic components were mounted to the chassis. The Arduino was wired to the motor driver (for drive motors) and the servo. The Bluetooth module was connected to the Arduino's serial pins for communication.
5.  **Code:** The Arduino was programmed to listen for commands via Bluetooth (e.g., 'f' for forward, 'l' for left) and translate them into motor and servo movements.

---

## 💻 Code

You can find all the code used in this project in the `/Code` directory.

- `/Code/Arduino/BB8_Control/` - The main sketch for the Arduino that handles motor control and Bluetooth communication.
- `/Code/App/` - (If applicable) Source code for the simple smartphone controller app.

### Dependencies

- `AFMotor.h` (if using an Adafruit Motor Shield)
- `SoftwareSerial.h` (for Bluetooth communication)

---

## 🚀 Usage

1.  Upload the `BB8_Control.ino` sketch to your Arduino.
2.  Power on the droid.
3.  Connect to the "HC-05" (or your module's name) device in your phone's Bluetooth settings.
4.  Open a Bluetooth serial terminal app and send commands:
    - `F`: Forward
    - `B`: Backward
    - `L`: Left
    - `R`: Right
    - `S`: Stop

---

## 📜 License

Copyright (c) 2025. All Rights Reserved.

This source code is made available for viewing and reference purposes only. It is not licensed for use, modification, or distribution.

_Disclaimer: This is a fan-made project. Star Wars and BB-8 are trademarks of Lucasfilm Ltd. and Disney._

# Discussion

should we use Prettier as Code formatter?
file -> preferences -> settings -> search for format on save -> enable it


# MediaMTX
 
/etc/systemd/system/mediamtx.service
- automatically starts the MediaMTX server in the background on boot
- distribute RTSP video streams to connecting clients

/etc/systemd/system/rtsp-stream.service
- automatically starts an FFmpeg process that captures raw video
- constantly streams it to the local MediaMTX server

get status:
```
systemctl status mediamtx.service
systemctl status rtsp-stream.service
```

Restart
```
sudo systemctl daemon-reload
sudo systemctl restart mediamtx.service
sudo systemctl restart rtsp-stream.service
```

# Starting up the system
## Hardware
1. Connect XT60 connectors and check all the connections on both power lines
2. Turn on the green power switch
3. Check the ESP32 breakout board if the power switch is turned on (esp32 receives power from battery)
4. Give one check over all the wire connections
    - Power jack to ESP32 breakout board
    - Power jack to NVIDIA Jetson Nano
5. 


## Software
### Terminal 1
1. ssh breakingbytes@100.93.171.127 | password: 
2. cd Documents/BB-8
3. docker-compose up -d
4. docker exec -it ros_control /bin/bash
5. cd root/ros_ws/
6. colcon build
7. source install/setup.bash
8. ros2 launch bb8_cmd_receiver bridge.launch.xml
- Looking to automate steps 5-7

<!-- ### Terminal 2 (Potentially Obsolete)
1. SSH breakingbytes@100.93.171.127 | password: 
2. cd Documents/BB-8
4. docker exec -it ros_control /bin/bash
5. cd root/ros_ws/
6. colon build
7. source install/setup.bash
8. ros2 run bb8_cmd_receiver command_receiver
- Writing a launch file to start command receiver from the same terminal as terminal 1 -->

# Terminal 3 - AI Inference
1. SSH breakingbytes@100.93.171.127 | password: 
2. cd Documents/BB-8
4. docker exec -it ai_inference /bin/bash
5. cd python
6. python3 -m face_service --network=facenet --headless --ws-port=9091 --threshold=0.8 --overlay=none --input-width=360 --input-height=240 --input=rtsp://100.95.33.109:8554/cam --input-codec=h264 --output=webrtc://@:8554/output

# Terminal 4 - Dashboard
1. cd to project repository root
2. cd SentryDashboard\sentry-dashboard
3. ng serve

