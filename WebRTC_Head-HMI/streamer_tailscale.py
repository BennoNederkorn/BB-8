import asyncio
import logging
import gc
from aiohttp import web
from aiortc import RTCPeerConnection, RTCSessionDescription, MediaStreamTrack
from aiortc.contrib.media import MediaPlayer
import aiohttp_cors

# Setup Logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("pc")

# GLOBAL VARIABLES
camera_lock = asyncio.Lock()  # Prevents multiple users from crashing the camera
current_pc = None             # Tracks the current active connection

class VideoTransformTrack(MediaStreamTrack):
    """
    Downsamples 30fps -> 15fps to reduce CPU load on Pi 3.
    """
    kind = "video"
    def __init__(self, track):
        super().__init__()
        self.track = track

    async def recv(self):
        # Read two frames, return one.
        await self.track.recv()
        return await self.track.recv()

async def cleanup_previous_connection():
    """
    Forcefully closes any existing connection and frees the camera.
    """
    global current_pc
    if current_pc:
        logger.info("Closing previous connection...")
        await current_pc.close()
        current_pc = None
        # Force Python to clear memory immediately
        gc.collect()

async def offer(request):
    global current_pc
    
    # 1. ACQUIRE LOCK (Wait if camera is busy)
    if camera_lock.locked():
        logger.warning("Request rejected: Camera is busy.")
        return web.Response(status=503, text="Camera is busy. Please try again in a second.")

    async with camera_lock:
        # 2. CLEANUP (Ensure no zombie connections exist)
        await cleanup_previous_connection()

        params = await request.json()
        pc = RTCPeerConnection()
        current_pc = pc

        # 3. INITIALIZE CAMERA (Fresh start for every user)
        # We start the camera INSIDE the lock to guarantee exclusive access
        try:
            player = MediaPlayer('/dev/video0', format='v4l2', options={
                'video_size': '320x240',
                'framerate': '30',
                # 'input_format': 'mjpeg', # MJPEG has lower encoding latency than raw on some Pis
                'fflags': 'nobuffer',        # KEY FIX: Do not buffer data
                'flags': 'low_delay',        # Optimize for low delay
                # 'probesize': '32',           # Read less data before starting
                # 'analyzeduration': '0'       # Start immediately
            })
        except Exception as e:
            logger.error(f"Camera Init Failed: {e}")
            return web.Response(status=500, text=f"Camera Error: {e}")

        # 4. CONNECTION LIFECYCLE
        @pc.on("connectionstatechange")
        async def on_connectionstatechange():
            logger.info(f"Connection state is {pc.connectionState}")
            if pc.connectionState in ["failed", "closed"]:
                await pc.close()
                if player and player.video:
                    player.video.stop()

        # Add video track (with 15fps filter)
        if player and player.video:
            pc.addTrack(VideoTransformTrack(player.video))

        # Handle SDP (Signaling)
        try:
            offer = RTCSessionDescription(sdp=params["sdp"], type=params["type"])
            await pc.setRemoteDescription(offer)
            answer = await pc.createAnswer()

            # 5. BANDWIDTH LIMIT (Fix for Mobile Hotspots)
            # Inject a 500kbps limit into the SDP answer
            sdp_lines = answer.sdp.splitlines()
            new_lines = []
            for line in sdp_lines:
                new_lines.append(line)
                if line.startswith("m=video"):
                    new_lines.append("b=AS:500") # 500 kbps limit
            
            answer_sdp_limited = "\r\n".join(new_lines) + "\r\n"
            await pc.setLocalDescription(RTCSessionDescription(sdp=answer_sdp_limited, type=answer.type))
        
        except Exception as e:
            logger.error(f"SDP Error: {e}")
            if player: player.video.stop()
            await pc.close()
            return web.Response(status=500, text=str(e))

        return web.json_response({
            "sdp": pc.localDescription.sdp,
            "type": pc.localDescription.type
        })

async def on_shutdown(app):
    await cleanup_previous_connection()

# --- APP SETUP ---
app = web.Application()
app.on_shutdown.append(on_shutdown)
app.router.add_post("/offer", offer)

# CORS (Allow Vercel)
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
    print("Server started. Listening...")
    web.run_app(app, host='0.0.0.0', port=8080)