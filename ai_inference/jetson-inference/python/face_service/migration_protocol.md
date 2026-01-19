# Face Service (BB-8) — Migration & Usage Guide

This document summarizes the face recognition migration, explains how the new modules work, and lists configuration knobs you can tune. It is written to mirror the doorbell demo logic while keeping the production pipeline modular and Jetson‑friendly.

---

## 1) What Changed (High‑Level)

We migrated the legacy, monolithic `inference_server.py` into a new **package**:

```
ai_inference/jetson-inference/python/face_service/
```

Key goals:

- **Face recognition** added on top of detected face ROIs (from detectNet / facenet).
- **Person naming** instead of generic `"face"` labels.
- **UNKNOWN** labeling for unrecognized faces.
- **5‑minute visit window** logic (doorbell demo behavior).
- **Persistent face database** in SQLite (instead of Python lists/pickle).
- **UI labeling flow** to promote unknowns to known identities.
- **Jetson Nano constraints** respected via throttling and caching.

---

## 2) New Package Layout

```
face_service/
├── __init__.py
├── __main__.py
├── inference_server.py
├── face_db.py
├── face_recognizer.py
├── visit_tracker.py
├── tools/
│   └── face_db_smoke.py
└── README.md
```

### Purpose of Each Module

- **`inference_server.py`**  
  Main entrypoint. Wraps the capture loop, handles input/output streams, calls detection and face recognition, and emits websocket events to the dashboard.

- **`face_db.py`**  
  SQLite database layer. Stores:
  - Persons (name, created_at)
  - Face encodings (BLOBs)
  - Visits (first_seen_this_interaction, last_seen, seen_count)
  - Unknown face snapshots (for labeling)

- **`face_recognizer.py`**  
  Encodes faces, matches them against cached encodings, and returns recognition results:
  - `name`
  - `distance`
  - `is_unknown`
  - `visit metadata`

- **`visit_tracker.py`**  
  Implements **5‑minute visit window** logic (same as the doorbell demo).

- **`tools/face_db_smoke.py`**  
  Lightweight smoke test to verify DB operations.

---

## 3) How It Maps to the Doorbell Demo

The doorbell demo had two in-memory lists:

- `known_face_encodings`
- `known_face_metadata`

We migrated that logic to a database + cache:

| Doorbell demo | Face Service |
|--------------|--------------|
| Python list of encodings | SQLite + in-memory cache |
| `register_new_face` | `FaceDatabase.add_person_encoding()` |
| `lookup_known_face` | `FaceRecognizer.match()` |
| `metadata["first_seen_this_interaction"]` | `VisitTracker` + DB visits |
| `seen_count`, `seen_frames` | DB visit counters + in-memory frame counts |

Behavior remains consistent:

- **Match threshold**: 0.65  
- **Same visit window**: 5 minutes  
- **UNKNOWN** label if no match

---

## 4) Data Storage

### Known faces (images)
Stored on disk:
```
ai_inference/jetson-inference/data/Faces/<Person Name>/
```

### Encodings + visits (database)
Stored in:
```
ai_inference/jetson-inference/data/faces.db
```

---

## 5) UI Integration (SentryDashboard)

The dashboard already supports name display using:

- `person_id`
- `is_unknown`

So the backend now emits:

- `person_id = "<Name>"`  
- `is_unknown = false`

or:

- `person_id = "UNKNOWN"`  
- `is_unknown = true`

A **Label button** is available in Security Log to label an unknown face, which:

1. Sends the selected face event ID + name.
2. Saves face crop into a new folder.
3. Inserts encoding into SQLite.
4. Future detections match this name.

---

## 6) How to Run (Jetson Container)

From the Jetson:

```bash
cd ~/Documents/BB-8/ai_inference/jetson-inference
docker/run.sh
```

Inside container:

```bash
cd /jetson-inference/python
python3 -m face_service \
  --network=facenet \
  --headless \
  --overlay=none \
  --input-width=360 \
  --input-height=240 \
  --threshold=0.8 \
  --input=webrtc://@:8554/input \
  --output=webrtc://@:8554/output
```

---

## 7) Why You Might See WebRTC Errors / Segfaults

Common causes:

- No producer running on `webrtc://@:8554/input`
- GStreamer pipeline timeout
- Resource pressure on Nano

Quick checks:

```bash
detectnet.py --headless webrtc://@:8554/input webrtc://@:8554/output
video-viewer webrtc://@:8554/input --headless
video-viewer csi://0 --headless
```

---

## 8) Configurations You Can Tune

### Recognition & Matching
- **Distance threshold**: default `0.65`
  - Lower = stricter matching
  - Higher = more matches, more false positives
- **Visit window**: fixed at **5 minutes**

### Performance / Jetson Load
- **Frame skip** (recognize every N frames)
- **Input resolution** (`--input-width/--input-height`)
- **Min face size** to ignore tiny detections

### UI / Event Frequency
- Debounce event emissions
- Limit max events per second

---

## 9) Smoke Test

From Jetson host:

```bash
cd ~/Documents/BB-8/ai_inference/jetson-inference/python
python3 -m face_service.tools.face_db_smoke
```

Expected:
```
known_count 1
```

---

## 10) Migration Summary (Checklist)

✅ Created `face_service` package  
✅ Split recognition into modules  
✅ Added SQLite database  
✅ Implemented visit tracking  
✅ Added unknown labeling + UI button  
✅ Kept doorbell demo logic intact  
✅ Added performance throttling for Nano  

---

## 11) Next Things To Be Improved

- Add batch encoding for new persons
- Add face clustering for unknowns
- Export CSV audit logs
- Add confidence display in dashboard
- Detecting two faces at once
- Detecting faces from afar

---
