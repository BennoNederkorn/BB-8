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
    * ip address: try 192.168.2.146
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
    ros2 run demo_nodes_cpp listener & ros2 run demo_nodes_cpp talker  
    ```
* **Step 11**: Build the AI Inference container
    ```bash
    cd /Documents/BB-8/ai_inference/jetson-inference
    docker/run.sh    
    ```
    If you are not prompted to download the pretrained models, run the following inside the container
    ```bash
    cd /jetson-inference/tools/
    ./download-models.sh
    ```




# Jetson Username and Password
```bash
username: breakingbytes
password: 12345
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
