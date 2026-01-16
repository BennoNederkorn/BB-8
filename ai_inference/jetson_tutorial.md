# Detailed step-by-step instruction on setting up the NVIDIA Jetson Nano
* **Step 1**: Flash the microSD card with an suitable image according to this [website from NVIDIA](https://developer.nvidia.com/embedded/learn/get-started-jetson-nano-devkit#write).
* **Step 2**: Continue the headed setup (with display attached). Follow the instructions [here](https://developer.nvidia.com/embedded/learn/get-started-jetson-nano-devkit#setup).
    * Keyboard layout is German, German
    * When prompted, choose ``MAXN``
* **Step 3**: Connect LAN cable to the NVIDIA Jetson Nano, and ensure that it blinks. You can run this in the terminal to sanity check network connections: ``ping 8.8.8.8`` 
* **Step 4**: Run in ther terminal of the Jetson and note down the ip address 
    ```bash
    sudo apt update
    sudo apt install openssh-server
    sudo systemctl enable --now ssh
    ip addr show
    ``` 
    * IP address: try 192.168.2.149
    * If the host identification has changed, run ``ssh-keygen -R 192.168.1.34`` 
* **Step 5**: In the host computer's terminal, run ``ssh breakingbytes@your_jetson_ip``
* **Step 6**: Once connected via SSH, run ``sudo systemctl set-default multi-user.target`` to tell Jetson to not boot the GUI. To reverse this run ``sudo systemctl set-default graphical.target``. 
* **Step 7**: Run ``sudo reboot``. This will cause the SSH connection to the Jetson to break. Reconnect after 15 seconds.
* **Step 8**: Run ``sudo usermod -aG docker $USER``. Logout by running ``logout`` and then connect back via SSH. Run ``docker run hello-world`` as a sanity check. 
* **Step 9**: In the ``Document`` directory, run ``git clone https://github.com/BennoNederkorn/BB-8.git``
* **Step 10**: Sanity check the ROS2 container (adaptation of this [tutorial](https://asciinema.org/a/A3UNWSfqs3AUQpLi1xSi8Mutr))
    ```bash
    cd /Documents/BB-8/
    sudo apt-get update
    sudo apt-get install -y python3-pip libssl-dev
    sudo pip3 install --upgrade pip
    sudo pip3 install docker-compose
    docker-compose --version
    docker-compose build ros-control
    docker-compose up -d ros-control && docker-compose exec ros-control /bin/bash
    ros2 run demo_nodes_cpp listener & ros2 run demo_nodes_cpp talker  
    ``` 
* **Step 11**: Build the AI Inference container
    ```bash
    cd /Documents/BB-8/ai_inference/jetson-inference
    docker/run.sh    
    ```
    If you are not prompted to download the pretrained models, run the following **inside the container**
    ```bash
    cd /jetson-inference/tools/
    ./download-models.sh
    ```
    If there was an error, it could be that the data/network is not a directory. For this, you have to delete that file and "remake" a new data/network directory

* **Step 12**: Generate SSL certificate and private key using ``openssl``. Run the following inside the Docker container as root:
    ```bash
    openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes -subj '/CN=localhost'
    ```

    What this command does:
    - ``req -x509``: Creates a self-signed certificate (standard for development/internal IPs).
    - ``newkey rsa:4096``: Generates a new RSA private key with 4096-bit strength.
    - ``keyout key.pem``: Saves the private key to key.pem (matches your export SSL_KEY).
    - ``out cert.pem``: Saves the certificate to cert.pem (matches your export SSL_CERT).
    - ``nodes: "No DES"`` – it creates an unencrypted key so the script can load it without asking you for a password every time (crucial for autonomous startup).
    - ``subj '/CN=localhost'``: Sets the "Common Name" to localhost, suppressing the interactive prompts

* **Step 13**: Export the SSL Keys and Certificates
    ```bash
    export SSL_KEY=/jetson-inference/data/key.pem
    export SSL_CERT=/jetson-inference/data/cert.pem
    ```
* **Step 14**: Run the detection network
    ```bash
    detectnet.py --headless webrtc://@:8554/input webrtc://@:8554/output    
    python3 inference_server.py --network=facenet --headless --overlay=none --input-width=360 --input-height=240 --threshold=0.8 --ssl-cert=/jetson-inference/data/cert.pem --ssl-key=/jetson-inference/data/key.pem webrtc://@:8554/input webrtc://@:8554/output

    python3 inference_server.py --network=facenet --headless --overlay=none --input-width=360 --input-height=240 --threshold=0.8 webrtc://@:8554/input webrtc://@:8554/output 

    video-viewer csi://0 webrtc://@:8554/output --headless
    ```

* **Step 15**: Run the dashboard FE and test the connection's latency
    - Ensure that the video capture is running on https://100.93.171.127:8554/
    - cd SentryDashboard\sentry-dashboard && ng serve

ros2 launch ros_deep_learning detectnet.ros2.launch input:=webrtc://@:8554/input output:=webrtc://@:8554/output


# Jetson Username and Password
```bash
username: breakingbytes
password: 12345
```

# Jetson Specifications
* **Jetpack Version**: 4.6

# Starting the rosbridge
```bash
ros2 launch rosbridge_server rosbridge_websocket_launch.xml delay_between_messages:=0.0
```

# Gracefully shutting down the Jetson
## Check and stop all running containers
```bash
docker ps
docker-compose down
docker stop $(docker ps -q)
```

## Manually flush all disk caches
```bash
sync
sleep 1
sync
```
## Initiate the system shutdown
```bash
    sudo shutdown -h now
```

# Update and upgrade packages
```bash
sudo apt update && sudo apt upgrade
```

# Troubleshooting
## SD Card not detected by the host computer
1. Press Windows Key + R to open the "Run" dialog.
2. Type ``diskmgmt.msc`` and press Enter. If the drive appears, but shows "RAW" or "Unallocated", continue
3. Open your Start menu, type cmd, right-click "Command Prompt", and select "Run as administrator".
4. Type ``diskpart`` and press Enter.
5. Type ``list disk`` and press Enter. You'll see a list of disks. Identify your SD card by its size (e.g., Disk 2 might be 29 GB)
6. Type ``select disk X`` (where X is the number of your SD card).
7. Type ``clean`` and press Enter. This will wipe the partition info.
8. Type ``create partition primary`` and press Enter.
9. Type ``exit`` and press Enter.

## SSH connection fails after rebooting (e.g., timeout)
1. Connect host computer to NVIDIA Jetson via microUSB and use PuTTy to connect to the console via serial bus. Navigate the same steps as this [tutorial](https://developer.nvidia.com/embedded/learn/get-started-jetson-nano-devkit#setup) for headless setup. 
2. If the microUSB connection is not showing as a serial connection on the host, pray that the system files did not get corrupted from reboot. You can check this by connecting the Jetson to a display and rebooting it. 

```bash
ssh-keygen -R 192.168.2.146
```

## Setting up the WIFI Connection 
1. Go to this sketchy website and download the linux installer: https://cat.eduroam.org/
2. Copy the direct link for the installer (this will install a python file)
3. SSH into Jetson and create a new file in home (e.g., via vim/nano) and copy the contents of the python file into this file
4. run ``sudo python FILENAME.py`` and it will prompt you to enter your username and password. This is the same for anything you do in TUM, i.e., for goXXXX id and password for it. The script authenticates you with the server and downloads the certifcate.
5. Next, create a bash script and paste the following contents into it. Change YOUR_LRZ_ID to the correct one.
    ```bash
    sudo nmcli con delete eduroam 2> /dev/null; \
    sudo nmcli con add \
    type wifi \
    ifname wlan0  \
    con-name "eduroam" \
    ssid "eduroam" \
    wifi-sec.key-mgmt wpa-eap \
    802-1x.eap peap \
    802-1x.phase2-auth mschapv2 \
    802-1x.identity "YOUR_LRZ_ID@eduroam.mwn.de" \
    802-1x.anonymous-identity "anonymous@eduroam.mwn.de" \
    802-1x.domain-suffix-match "radius.lrz.de" \
    802-1x.ca-cert ~/.config/cat_installer/ca.pem \
    802-1x.password ''
    ```
6. Run this bash file with sudo. If successful, you will get "Connection 'eduroam' (...) successfully added".
7. Then finally, run this ``nmcli con up eduroam --ask``

## WIFI Connection Failing  
1. Delete the connection ``sudo nmcli connection delete "MagentaWLAN-DXPQ"``
1. Unplug the wifi router and plug it back in
2. Scan the network and see what it can detect: ``sudo nmcli device wifi list``
3. Force the connection to 2.4 GHz (try Channel 11)
4. Find the BSSID (MAC Address) for the 2.4 GHz
    ```bash
    sudo nmcli -f SSID,BSSID,CHAN,SIGNAL dev wifi list
    ```
5. Connect using the BSSID
    ```
    sudo nmcli dev wifi connect "MagentaWLAN-DXPQ" password '86842289257325324858'

    08:A7:C0:DC:51:08
    ```
6. Verify the Connection    
    ```bash
    ip a show wlan0
    ```



docker-compose up -d --build
docker-compose up -d
docker exec -it ai_inference /bin/bash