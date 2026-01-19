#!/usr/bin/env python3
"""
Async WebSocket broadcaster for Jetson inference results with face recognition.
Compatible with Python 3.6 on Jetson Nano using asyncio + websockets.
"""

import argparse
import asyncio
import base64
import json
import math
import os
import random
import threading
import time
from datetime import timedelta
from typing import Any, Dict, Optional, Set, Tuple

import cv2
import numpy as np

import jetson.inference
import jetson.utils
import websockets

from .face_db import FaceDatabase
from .face_recognizer import FaceRecognizer
from .visit_tracker import VisitTracker


def _read_cpu_temp_c() -> float:
    path = "/sys/devices/virtual/thermal/thermal_zone0/temp"
    try:
        with open(path, "r") as f:
            milli_c = int(f.read().strip())
            return milli_c / 1000.0
    except Exception:
        return 45.0 + random.random() * 5.0


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
    return 35.0 + random.random() * 10.0


def _read_gpu_load_pct() -> float:
    path = "/sys/devices/gpu.0/load"
    try:
        with open(path, "r") as f:
            raw = float(f.read().strip())
            pct = max(0.0, min(100.0, raw / 10.0))
            return round(pct, 2)
    except Exception:
        return 0.0 + random.random() * 5.0


async def broadcast_loop(
    queue: "asyncio.Queue[Tuple[str, Dict[str, Any]]]",
    clients: Set[websockets.WebSocketServerProtocol],
    stop_event: threading.Event,
) -> None:
    while not stop_event.is_set():
        try:
            topic, payload = await asyncio.wait_for(queue.get(), timeout=0.25)
        except asyncio.TimeoutError:
            continue
        message = json.dumps({"topic": topic, "payload": payload})
        stale = []
        for ws in list(clients):
            try:
                await ws.send(message)
            except Exception:
                stale.append(ws)
        for ws in stale:
            clients.discard(ws)


async def telemetry_loop(
    queue: "asyncio.Queue[Tuple[str, Dict[str, Any]]]",
    stop_event: threading.Event,
) -> None:
    while not stop_event.is_set():
        payload = {
            "cpu_temp": round(_read_cpu_temp_c(), 2),
            "ram_usage": round(_read_ram_usage_pct(), 2),
            "gpu_load": round(_read_gpu_load_pct(), 2),
            "inference_fps": 0.0,
            "state": "ARMED",
        }
        await queue.put(("/system/status", payload))
        await asyncio.sleep(1.0)


async def command_loop(
    command_queue: "asyncio.Queue[Dict[str, Any]]",
    queue: "asyncio.Queue[Tuple[str, Dict[str, Any]]]",
    db: FaceDatabase,
    recognizer: FaceRecognizer,
) -> None:
    while True:
        command = await command_queue.get()
        topic = command.get("topic")
        payload = command.get("payload", {})
        if topic == "/camera/label_face":
            unknown_id = payload.get("unknown_id")
            name = payload.get("name")
            result = None
            if unknown_id and name:
                result = db.label_unknown(unknown_id, name)
                if result is not None:
                    recognizer.load_known_faces()
            await queue.put((
                "/camera/label_face_ack",
                {"unknown_id": unknown_id, "name": name, "success": result is not None},
            ))


async def client_handler(
    websocket: websockets.WebSocketServerProtocol,
    clients: Set[websockets.WebSocketServerProtocol],
    command_queue: "asyncio.Queue[Dict[str, Any]]",
) -> None:
    clients.add(websocket)
    try:
        async for message in websocket:
            try:
                parsed = json.loads(message)
            except Exception:
                continue
            if isinstance(parsed, dict) and "topic" in parsed:
                await command_queue.put(parsed)
    finally:
        clients.discard(websocket)


def _encode_roi_b64(roi_bgr: np.ndarray) -> str:
    ok, buffer = cv2.imencode(".jpg", roi_bgr)
    if not ok:
        return ""
    return base64.b64encode(buffer).decode("ascii")


class RecognitionState:
    def __init__(self) -> None:
        self.last_recognition_time = 0.0
        self.last_unknown_time = 0.0


def inference_worker(
    loop: asyncio.AbstractEventLoop,
    queue: "asyncio.Queue[Tuple[str, Dict[str, Any]]]",
    stop_event: threading.Event,
    args: argparse.Namespace,
    db: FaceDatabase,
    recognizer: FaceRecognizer,
    tracker: VisitTracker,
) -> None:
    net = jetson.inference.detectNet(args.network, args.argv, args.threshold)
    source = jetson.utils.videoSource(args.input, argv=args.argv)
    output = jetson.utils.videoOutput(args.output, argv=args.argv)
    state = RecognitionState()

    while not stop_event.is_set():
        try:
            img = source.Capture()
            if img is None:
                continue

            detections = net.Detect(img, overlay="none")
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
                np_img = jetson.utils.cudaToNumpy(img)
                height, width = np_img.shape[0], np_img.shape[1]

                now = time.time()
                should_recognize = (now - state.last_recognition_time) >= args.recognition_interval
                if should_recognize:
                    state.last_recognition_time = now

                for det in detections:
                    x1 = max(0, int(det.Left))
                    y1 = max(0, int(det.Top))
                    x2 = min(width, int(det.Right))
                    y2 = min(height, int(det.Bottom))

                    if (x2 - x1) < args.min_face_size or (y2 - y1) < args.min_face_size:
                        continue

                    roi_b64 = ""
                    roi_bgr = None
                    try:
                        crop = np_img[y1:y2, x1:x2]
                        if crop.size > 0:
                            if crop.shape[2] == 4:
                                roi_bgr = cv2.cvtColor(crop, cv2.COLOR_RGBA2BGR)
                            else:
                                roi_bgr = cv2.cvtColor(crop, cv2.COLOR_RGB2BGR)
                            roi_b64 = _encode_roi_b64(roi_bgr)
                    except Exception:
                        roi_b64 = ""

                    if not should_recognize or roi_bgr is None:
                        continue

                    encoding = recognizer.encode_face(roi_bgr, scale=args.recognition_scale)
                    if encoding is None:
                        continue

                    match = recognizer.match(encoding)
                    unknown_id: Optional[str] = None
                    if match is None:
                        if (now - state.last_unknown_time) >= args.unknown_cooldown:
                            unknown_id = db.add_unknown_face(encoding, roi_bgr)
                            state.last_unknown_time = now
                        payload = {
                            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                            "person_id": "UNKNOWN",
                            "confidence": round(det.Confidence, 4),
                            "is_unknown": True,
                            "roi_crop": roi_b64,
                            "unknown_id": unknown_id,
                        }
                    else:
                        visit = tracker.update(match["person_id"])
                        payload = {
                            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                            "person_id": match["name"],
                            "confidence": round(det.Confidence, 4),
                            "is_unknown": False,
                            "roi_crop": roi_b64,
                            "visit_count": visit["seen_count"],
                            "seconds_at_door": visit["seconds_at_door"],
                        }

                    loop.call_soon_threadsafe(queue.put_nowait, ("/camera/face_events", payload))

            output.Render(img)
        except Exception:
            continue


def _default_faces_dir() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", "..", "data", "Faces"))


def main() -> None:
    parser = argparse.ArgumentParser(description="Jetson inference WebSocket broadcaster with face recognition")
    parser.add_argument("--network", default="facenet", help="detectNet/faceNet model name")
    parser.add_argument("--input", default="webrtc://@:8554/input", help="video input URI")
    parser.add_argument("--output", default="webrtc://@:8554/output", help="video output URI")
    parser.add_argument("--threshold", type=float, default=0.4, help="detection threshold")
    parser.add_argument("--ws-port", type=int, default=9090, help="WebSocket port")
    parser.add_argument("--ssl-cert", type=str, default=None, help="path to SSL cert for WebRTC")
    parser.add_argument("--ssl-key", type=str, default=None, help="path to SSL key for WebRTC")
    parser.add_argument("--faces-dir", type=str, default=_default_faces_dir(), help="root folder containing known face subfolders")
    parser.add_argument("--db-path", type=str, default=None, help="SQLite database path")
    parser.add_argument("--recognition-interval", type=float, default=0.75, help="seconds between recognition runs")
    parser.add_argument("--recognition-scale", type=float, default=0.5, help="scale factor applied to ROI for recognition")
    parser.add_argument("--min-face-size", type=int, default=40, help="minimum ROI size to run recognition")
    parser.add_argument("--unknown-cooldown", type=float, default=3.0, help="seconds between unknown face samples")
    parser.add_argument("--tolerance", type=float, default=0.65, help="face distance threshold for match")
    parser.add_argument("--no-sync-known-faces", dest="sync_known_faces", action="store_false")
    parser.set_defaults(sync_known_faces=True)

    args, extra = parser.parse_known_args()
    args.argv = extra

    if args.db_path is None:
        args.db_path = os.path.join(args.faces_dir, "face_db.sqlite3")

    db = FaceDatabase(args.db_path, args.faces_dir)
    recognizer = FaceRecognizer(db, tolerance=args.tolerance)
    recognizer.load_known_faces()
    if args.sync_known_faces:
        try:
            recognizer.sync_from_faces_dir()
        except Exception:
            pass
    tracker = VisitTracker(db, timedelta(minutes=5))

    loop = asyncio.get_event_loop()
    asyncio.set_event_loop(loop)
    queue: asyncio.Queue[Tuple[str, Dict[str, Any]]] = asyncio.Queue()
    command_queue: asyncio.Queue[Dict[str, Any]] = asyncio.Queue()
    clients: Set[websockets.WebSocketServerProtocol] = set()
    stop_event = threading.Event()

    ws_server = websockets.serve(
        lambda ws, _path: client_handler(ws, clients, command_queue),
        "0.0.0.0",
        args.ws_port,
        loop=loop,
    )
    loop.run_until_complete(ws_server)

    loop.create_task(broadcast_loop(queue, clients, stop_event))
    loop.create_task(telemetry_loop(queue, stop_event))
    loop.create_task(command_loop(command_queue, queue, db, recognizer))

    worker = threading.Thread(
        target=inference_worker,
        args=(loop, queue, stop_event, args, db, recognizer, tracker),
        daemon=True,
    )
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
                pending = list(pending_iter) if pending_iter is not None else []
            except Exception:
                pending = []
        elif hasattr(asyncio, "all_tasks"):
            try:
                pending_iter = asyncio.all_tasks(loop=loop)
                pending = list(pending_iter) if pending_iter is not None else []
            except Exception:
                pending = []
        for task in pending:
            task.cancel()
        loop.stop()
        db.close()


if __name__ == "__main__":
    main()
