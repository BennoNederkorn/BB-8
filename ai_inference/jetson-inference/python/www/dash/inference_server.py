#!/usr/bin/env python3
"""
Async WebSocket broadcaster for Jetson inference results and telemetry.
Compatible with Python 3.6 on Jetson Nano (L4T base image) using asyncio + websockets.
Video is streamed separately over WebRTC via jetson.utils.videoOutput.
"""

import argparse
import asyncio
import json
import os
import random
import threading
import time
import sys
import math
import base64
from typing import Any, Dict, Set, Tuple

import cv2
import numpy as np

import jetson.inference
import jetson.utils
import websockets

# -----------------------------
# Helpers for system telemetry
# -----------------------------

def _read_cpu_temp_c() -> float:
    path = "/sys/devices/virtual/thermal/thermal_zone0/temp"
    try:
        with open(path, "r") as f:
            milli_c = int(f.read().strip())
            return milli_c / 1000.0
    except Exception:
        return 45.0 + random.random() * 5.0  # fallback placeholder


def _read_ram_usage_pct() -> float:
    meminfo = {}
    try:
        with open("/proc/meminfo", "r") as f:
            for line in f:
                parts = line.strip().split(":")
                if len(parts) == 2:
                    key, val = parts
                    meminfo[key] = float(val.strip().split()[0])
        mem_total = meminfo.get("MemTotal", 0.0)
        mem_available = meminfo.get("MemAvailable", mem_total)
        if mem_total > 0:
            used = mem_total - mem_available
            return (used / mem_total) * 100.0
    except Exception:
        pass
    return 35.0 + random.random() * 10.0  # fallback placeholder


def _read_gpu_load_pct() -> float:
    """Read GPU load percentage from sysfs. Jetson reports 0-1000 (tenths of a percent)."""
    path = "/sys/devices/gpu.0/load"
    try:
        with open(path, "r") as f:
            raw = float(f.read().strip())
            # Convert from 0-1000 range to percentage
            pct = max(0.0, min(100.0, raw / 10.0))
            return round(pct, 2)
    except Exception:
        return 0.0 + random.random() * 5.0  # lightweight fallback placeholder


# -----------------------------
# Broadcast utility
# -----------------------------
async def broadcast_loop(queue: "asyncio.Queue[Tuple[str, Dict[str, Any]]]", clients: Set[websockets.WebSocketServerProtocol], stop_event: threading.Event) -> None:
    """Consume messages from queue and fan-out to all connected clients."""
    while not stop_event.is_set():
        topic, payload = await queue.get()
        message = json.dumps({"topic": topic, "payload": payload})
        stale = []
        for ws in list(clients):
            try:
                await ws.send(message)
            except Exception:
                stale.append(ws)
        for ws in stale:
            clients.discard(ws)


async def telemetry_loop(queue: "asyncio.Queue[Tuple[str, Dict[str, Any]]]") -> None:
    """Push telemetry at 1 Hz."""
    while True:
        payload = {
            "cpu_temp": round(_read_cpu_temp_c(), 2),
            "ram_usage": round(_read_ram_usage_pct(), 2),
            "gpu_load": round(_read_gpu_load_pct(), 2),
            "inference_fps": 0.0,  # updated by inference worker via queue
            "state": "ARMED",
        }
        await queue.put(("/system/status", payload))
        await asyncio.sleep(1.0)


# -----------------------------
# Inference worker (thread)
# -----------------------------

def inference_worker(loop: asyncio.AbstractEventLoop, queue: "asyncio.Queue[Tuple[str, Dict[str, Any]]]", stop_event: threading.Event, args: argparse.Namespace) -> None:
    """Capture -> infer -> render, forwarding events into the asyncio queue."""
    # align with detectnet.py: let jetson.utils parse CLI flags (e.g., WebRTC codec negotiation)
    net = jetson.inference.detectNet(args.network, sys.argv, args.threshold)
    source = jetson.utils.videoSource(args.input, argv=sys.argv)
    output = jetson.utils.videoOutput(args.output, argv=sys.argv)

    while not stop_event.is_set():
        try:
            img = source.Capture()
            if img is None:
                continue

            detections = net.Detect(img, overlay='none')
            fps = net.GetNetworkFPS()
            if math.isinf(fps) or math.isnan(fps):
                fps = 0.0
            loop.call_soon_threadsafe(queue.put_nowait, ("/system/status", {
                "cpu_temp": round(_read_cpu_temp_c(), 2),
                "ram_usage": round(_read_ram_usage_pct(), 2),
                "gpu_load": round(_read_gpu_load_pct(), 2),
                "inference_fps": round(fps, 2),
                "state": "ARMED",
            }))

            if len(detections) > 0:
                # copy CUDA image to host for ROI encoding
                np_img = jetson.utils.cudaToNumpy(img)
                height, width = np_img.shape[0], np_img.shape[1]

                for det in detections:
                    class_id = det.ClassID
                    x1 = max(0, int(det.Left))
                    y1 = max(0, int(det.Top))
                    x2 = min(width, int(det.Right))
                    y2 = min(height, int(det.Bottom))

                    roi_b64 = ""
                    try:
                        crop = np_img[y1:y2, x1:x2]
                        if crop.size > 0:
                            # convert RGBA/RGB to BGR for OpenCV encoding
                            if crop.shape[2] == 4:
                                crop = cv2.cvtColor(crop, cv2.COLOR_RGBA2BGR)
                            else:
                                crop = cv2.cvtColor(crop, cv2.COLOR_RGB2BGR)

                            ok, buffer = cv2.imencode('.jpg', crop)
                            if ok:
                                roi_b64 = base64.b64encode(buffer).decode('ascii')
                    except Exception:
                        roi_b64 = ""

                    payload = {
                        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                        "person_id": net.GetClassDesc(class_id) if net.GetClassDesc(class_id) else f"ClassID-{class_id}",
                        "confidence": round(det.Confidence, 4),
                        "is_unknown": False if class_id >= 0 else True,
                        "roi_crop": roi_b64,
                    }
                    loop.call_soon_threadsafe(queue.put_nowait, ("/camera/face_events", payload))

            output.Render(img)
            # Disable status bar overlay to remove text on stream
            # output.SetStatus(f"{fps:.1f} FPS | {args.network}")
        except Exception:
            # keep running even if a frame fails
            continue


# -----------------------------
# WebSocket server
# -----------------------------
async def client_handler(websocket: websockets.WebSocketServerProtocol, path: str, clients: Set[websockets.WebSocketServerProtocol]) -> None:
    clients.add(websocket)
    try:
        await websocket.wait_closed()
    finally:
        clients.discard(websocket)


# -----------------------------
# Main
# -----------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Jetson inference WebSocket broadcaster")
    parser.add_argument("--network", default="facenet", help="detectNet/faceNet model name")
    parser.add_argument("--input", default="webrtc://@:8554/input", help="video input URI")
    parser.add_argument("--output", default="webrtc://@:8554/output", help="video output URI")
    parser.add_argument("--threshold", type=float, default=0.4, help="detection threshold")
    parser.add_argument("--ws-port", type=int, default=9090, help="WebSocket port")
    parser.add_argument("--ssl-cert", type=str, default=None, help="path to SSL cert for WebRTC")
    parser.add_argument("--ssl-key", type=str, default=None, help="path to SSL key for WebRTC")
    # allow extra jetson-utils args (e.g., --headless, codec flags) to pass through
    args, _ = parser.parse_known_args()

    loop = asyncio.get_event_loop()
    asyncio.set_event_loop(loop)
    queue: asyncio.Queue[Tuple[str, Dict[str, Any]]] = asyncio.Queue()
    clients: Set[websockets.WebSocketServerProtocol] = set()
    stop_event = threading.Event()

    # start websocket server
    ws_server = websockets.serve(lambda ws, path: client_handler(ws, path, clients), "0.0.0.0", args.ws_port, loop=loop)
    loop.run_until_complete(ws_server)

    # start background broadcaster and telemetry
    loop.create_task(broadcast_loop(queue, clients, stop_event))
    loop.create_task(telemetry_loop(queue))

    # start inference thread
    worker = threading.Thread(target=inference_worker, args=(loop, queue, stop_event, args), daemon=True)
    worker.start()

    try:
        loop.run_forever()
    except KeyboardInterrupt:
        stop_event.set()
    finally:
        stop_event.set()
        worker.join(timeout=2.0)
        for ws in list(clients):
            try:
                loop.run_until_complete(ws.close())
            except Exception:
                pass
        pending = []
        all_tasks = getattr(asyncio.Task, "all_tasks", None)
        if callable(all_tasks):
            try:
                pending_iter = all_tasks(loop=loop)
                pending = list(pending_iter) if pending_iter is not None else []  # type: ignore[arg-type]
            except Exception:
                pending = []
        elif hasattr(asyncio, "all_tasks"):
            try:
                pending_iter = asyncio.all_tasks(loop=loop)  # type: ignore[arg-type]
                pending = list(pending_iter) if pending_iter is not None else []  # type: ignore[arg-type]
            except Exception:
                pending = []
        for task in pending:
            task.cancel()
        loop.stop()


if __name__ == "__main__":
    main()
