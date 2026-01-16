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