### Installation:

- Follow these instructions https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-macos-setup.html#get-started-linux-macos-first-steps
  - We use the esp32 for the body and the esp32s3 for the Head.

### Project:

- don't forget to add the ESP tools to the PATH environment variable: `. $HOME/esp/esp-idf/export.sh`
- After opening a new project, you should first set the target with
  - `idf.py set-target esp32` or
  - `idf.py set-target esp32s3`
- optional `idf.py menuconfig` to set up project specific variables, e.g., Wi-Fi network name and password, the processor speed, etc
- build: `idf.py build`
- flash to device: `idf.py -p PORT flash`
  - if the port is not found hold "Boot", click "Reset", release "Boot"
  - run `ls /dev/ttyACM*` and `ls /dev/ttyUSB*` to find the correct port
  - if the flashing fails, try to execute the command while holding the boot button
- `idf.py -p PORT monitor`
  - press the Reset button to boot from the flash
  - close the serial monitor with `Ctrl+]`

### VsCode:

- install the ESP-IDF extension in VS-Code
- In VS code click F1 -> type "ESP-IDF: Configure ESP-IDF Extension"
  - choose Express
  - TODO
