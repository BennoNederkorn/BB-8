### Installaitions:
Install Tailscale so that the Angular app can talk directly to the Raspberry Pi via HTTP to exchange the WebRTC handshake.
```
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```


```
pip3 install aiohttp aiortc opencv-python aiohttp-cors
```


### Tailscale

Publish the web server running privately on the Raspberry Pi and securely share it with everyone else on the Tailscale network. This command turns the Raspberry Pi into a secure web server only accessible by all Tailscale devices, but not the entire public internet:

```
sudo tailscale serve --bg http://127.0.0.1:8080
```

- ```http://127.0.0.1:8080``` is the resource which will be shared
- the ```/``` put the shared object at the main address (e.g. https://my-laptop.tailnet.ts.net/), not a sub-folder
- ```-bg``` puts the server in the backgroung.
- ``` tailscale serve status``` shows all the shared resources.
- ``` tailscale serve reset``` stops the sharering of the resources.

Browsers will block connections if a request is sent from HTTPS to HTTP. This is called Mixed Content Error. This happens because the HMI (hosted by Vercel) is served over HTTPS but the Raspberry Pi's Tailscale IP is typically HTTP. To fix this, Tailscale Funnel can expose a specific port from the Raspberry Pi to the public internet with an automatic HTTPS certificate.
```
sudo tailscale funnel -bg --https=443 http://127.0.0.1:8080
```


### Architecture
- Angular: Sends an HTTP POST request (Offer) directly to https://your-pi.tailnet.ts.net/offer.
- Pi: Receives the request, generates an Answer, and sends it back in the HTTP response.
- WebRTC: Establish the video connection.

## Raspberry Pi Hardware and Software Configuration
### General System & OS Information
- Model Name: Raspberry Pi 3 Model B Rev 1.2 
- OS Release: 
    ```bash
    PRETTY_NAME="Debian GNU/Linux 13 (trixie)"
    NAME="Debian GNU/Linux"
    VERSION_ID="13"
    VERSION="13 (trixie)"
    VERSION_CODENAME=trixie
    DEBIAN_VERSION_FULL=13.2
    ID=debian
    HOME_URL="https://www.debian.org/"
    SUPPORT_URL="https://www.debian.org/support"
    BUG_REPORT_URL="https://bugs.debian.org/"
    ```
- Kernel Version: 
    Linux raspberrypi 6.12.47+rpt-rpi-v8 #1 SMP PREEMPT Debian 1:6.12.47-1+rpt1 (2025-09-16) aarch64 GNU/Linux
- Firmware Version: 
    ```bash
    Aug 20 2025 17:04:09
    Copyright (c) 2012 Broadcom
    version cd866525580337c0aee4b25880e1f5f9f674fb24 (clean) (release) (start)
    ```

### CPU & Hardware Specifics
- CPU Info
    ```bash
    processor       : 0
    BogoMIPS        : 38.40
    Features        : fp asimd evtstrm crc32 cpuid
    CPU implementer : 0x41
    CPU architecture: 8
    CPU variant     : 0x0
    CPU part        : 0xd03
    CPU revision    : 4

    processor       : 1
    BogoMIPS        : 38.40
    Features        : fp asimd evtstrm crc32 cpuid
    CPU implementer : 0x41
    CPU architecture: 8
    CPU variant     : 0x0
    CPU part        : 0xd03
    CPU revision    : 4

    processor       : 2
    BogoMIPS        : 38.40
    Features        : fp asimd evtstrm crc32 cpuid
    CPU implementer : 0x41
    CPU architecture: 8
    CPU variant     : 0x0
    CPU part        : 0xd03
    CPU revision    : 4

    processor       : 3
    BogoMIPS        : 38.40
    Features        : fp asimd evtstrm crc32 cpuid
    CPU implementer : 0x41
    CPU architecture: 8
    CPU variant     : 0x0
    CPU part        : 0xd03
    CPU revision    : 4

    Revision        : a02082
    Serial          : 00000000fb57aeee
    Model           : Raspberry Pi 3 Model B Rev 1.2
    ```

### Memeory (RAM) & Storage 
- Memory Split (CPU vs GPU):
    - arm=948M
    - gpu=76M



