# How the WebRTC code works?

## Phase 1: The Offer

Signaling:

- This is the process of exchanging metadata to coordinate communication.
- WebRTC doesn't specify a signaling method, so you have to use a WebSocket server for this.
- The messages exchanged are SDP (Session Description Protocol) offers/answers.

### 0 ESP32:

- The ESP32 connects to WiFi
- It configures and initiates a secure, auto-reconnecting WebSocket connection to the signaling server.
- It register a `websocket_event_handler` to handle all events like:
  - a answer from the signaling server
  - SDP offers
  - ICE candidates

### 1 WebApp: User Clicks "Connect"

- which calles `onClick()`
- A new `RTCPeerConnection` is created. This is the browser's representation of the connection.
- A WebSocket connection is established with your signaling server.

### 2 WebApp: Create and Send Offer

- The WebSocket's `onopen` event fires.
- It calls `peerConnection.createOffer()`
  - It generates an SDP Offer. This is a text block describing what the WebApp wants to do:
  - The WebApp wants to receive a video and won't be sending any.
- It also calls `peerConnection.setLocalDescription(offer)`
  - It tells the browser to commit to this offer.
  - It also triggers the ICE gathering process.
- In the end sends the WebApp the SDP offer to the signaling server, wrapped in a JSON object

### 3 Signaling Server: Broadcast

- The Signaling Server receives the JSON message and broadcasts it to all connected clients, including your ESP32.

### 4 ESP32: Receive Offer and Create Peer

- The `websocket_event_handler` on the ESP32 receives the SDP offer message.
- It parses the JSON and finds the "offer" key.
- This is the signal to start the WebRTC process.
- It calls `esp_peer_open()` to create its own peer connection instance.
  - It sets its role to `ESP_PEER_ROLE_CONTROLLED` (the one that answers).
- It passes the received SDP offer string to the new peer instance using `esp_peer_send_msg()`

## Phase 2: The Answer & ICE Candidate Exchange (Happens in Parallel)

- The WebApp and ESP32 now both have a "local description" (SDP offer) and start doing two things simultaneously:
  - gathering network candidates (ICE)
  - preparing a response (the Answer).

### 5 ESP32: Create and Send Answer

- When the ESP32 peer processes the SDP offer from the WebApp, it generates its own SDP Answer which includes:
  - offer acceptence to receive video.
  - send video on a data channel.
  - used codecs
- After that ESP32 sends that SDP answer back to the signaling server
  - the `on_peer_msg` callback is triggered on the ESP32 with `msg->type == ESP_PEER_MSG_TYPE_SDP`.
  - wraps this SDP answer in a JSON object `({ "answer": "sdp_string..." })`.

### 6 Parallel Action: ICE Candidate Gathering

WebApp: As soon as setLocalDescription was called in step 2, the browser started gathering ICE candidates. For each candidate found (a potential network path), the peerConnection.onicecandidate event fires. Your code sends each candidate to the signaling server: { "candidate": {...} }.
ESP32: Similarly, after processing the offer, the ESP32's peer library starts gathering its own ICE candidates. For each one, your on_peer_msg callback is triggered with msg->type == ESP_PEER_MSG_TYPE_CANDIDATE. Your code sends these to the signaling server as well.
Signaling & Adding Candidates

The signaling server continues to act as a simple relay, broadcasting all the answer and candidate messages.
WebApp: When it receives an "answer" from the ESP32, it calls peerConnection.setRemoteDescription(answer). It now knows the ESP32's capabilities.
WebApp: When it receives a "candidate" from the ESP32, it adds it to its connection using peerConnection.addIceCandidate(candidate).
ESP32: When it receives a "candidate" from the WebApp, your websocket_event_handler correctly calls esp_peer_send_msg() to add it to its peer connection.
Phase 3: Connection and Streaming
Connectivity Checks

With a list of local and remote candidates, both peers start sending STUN packets to each other to check which network path works. This is the "Interactive Connectivity Establishment" in action.
Connection Established!

Once a working path is found, the peer connection state changes to "connected".
On the ESP32, this triggers your on_peer_state_change callback with ESP_PEER_STATE_DATA_CHANNEL_OPENED. This is your green light!
Your code correctly sets the RTC_CONNECTED_BIT and creates the video_stream_task.
Video Streaming

The video_stream_task starts running.
It captures a JPEG frame from the camera (esp_camera_fb_get).
It sends the raw JPEG data over the data channel using esp_peer_send_data().
On the WebApp, the ondatachannel event has already fired, and its onmessage handler now receives the JPEG data as a Blob.
Your initializeCanvasStream logic correctly takes this blob, creates an image from it, draws it to a hidden canvas, and then streams the canvas content to your <video> element. This is the standard and correct way to handle a custom video-over-datachannel stream.
Why are WebSocket Messages Fragmented?
You noticed in your ESP32 code that you need to reassemble WebSocket messages. This is an important detail.

A single WebSocket message can be split into multiple "frames" at the transport layer. Your websocket_event_handler is called for each frame, not for each complete message.

data->data_len is the size of the current frame's payload.
data->payload_len is the size of the total message payload.
data->fin (a bit in data->op_code) is a flag that is true only for the final frame of a message.
Your current reassembly logic, which checks if the buffer ends with }, is a good start but can be fragile. A more robust approach is to check if the total received data length equals data->payload_len.

Here is a suggested improvement for your websocket_event_handler to handle fragmentation more reliably.

### References

- https://components.espressif.com/components/espressif/esp32_s3_eye/versions/2.0.2/dependencies?language=en
- https://github.com/espressif/esp-who/blob/master/docs/en/get-started/ESP32-S3-EYE_Getting_Started_Guide.md
- https://github.com/espressif/esp32-camera/tree/master/examples/camera_example
- https://github.com/platformio/platform-espressif32/issues/1535
