import asyncio
import json
import logging
import uuid
from aiohttp import web
from aiortc import RTCPeerConnection, RTCSessionDescription
from aiortc.contrib.media import MediaPlayer
import aiohttp_cors

# Setup Logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("pc")

# 1. Initialize the Camera ONCE at startup (Global Scope)
# This prevents the "Device busy" error on page refresh
try:
    player = MediaPlayer('/dev/video0', format='v4l2', options={
        'video_size': '640x480',
        'framerate': '30'
    })
    print("Camera initialized successfully.")
except Exception as e:
    print(f"Error initializing camera: {e}")
    exit(1)

# Keep track of active connections so we can clean them up
pcs = set()

async def offer(request):
    params = await request.json()
    
    # 2. Create a NEW PeerConnection for every request
    # This ensures if you refresh the page, you get a fresh connection state
    pc = RTCPeerConnection()
    pcs.add(pc)

    # Clean up when connection closes
    @pc.on("connectionstatechange")
    async def on_connectionstatechange():
        print(f"Connection state is {pc.connectionState}")
        if pc.connectionState == "failed" or pc.connectionState == "closed":
            await pc.close()
            pcs.discard(pc)

    # 3. Add the EXISTING player track to this new connection
    # We reuse the video stream we opened at the top
    if player and player.video:
        pc.addTrack(player.video)

    # Set Remote Description (The Offer from Angular)
    offer = RTCSessionDescription(sdp=params["sdp"], type=params["type"])
    await pc.setRemoteDescription(offer)

    # Create Answer
    answer = await pc.createAnswer()
    await pc.setLocalDescription(answer)

    return web.json_response({
        "sdp": pc.localDescription.sdp,
        "type": pc.localDescription.type
    })

async def on_shutdown(app):
    # Close all peer connections
    coros = [pc.close() for pc in pcs]
    await asyncio.gather(*coros)
    # Don't forget to close the camera explicitly at the very end
    if player:
        player.video.stop()

app = web.Application()
app.on_shutdown.append(on_shutdown)
app.router.add_post("/offer", offer)

# CORS Setup
cors = aiohttp_cors.setup(app, defaults={
    "*": aiohttp_cors.ResourceOptions(
            allow_credentials=True,
            expose_headers="*",
            allow_headers="*",
        )
})
for route in list(app.router.routes()):
    cors.add(route)

if __name__ == "__main__":
    print("Server started. Listening for connections...")
    web.run_app(app, host='127.0.0.1', port=8080)
