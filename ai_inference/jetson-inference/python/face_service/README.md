# Face Service

Async WebSocket broadcaster that performs face detection + recognition and reports named events to the dashboard.

## Features
- Recognizes known faces stored under `data/Faces/<Person Name>/`
- Stores encodings + visit metadata in SQLite (`face_db.sqlite3`)
- Labels unknown faces as `UNKNOWN` and supports UI labeling
- Throttles recognition for Jetson Nano resources

## Run
From the Jetson container:
```bash
python3 -m face_service \
  --network=facenet \
  --input=webrtc://@:8554/input \
  --output=webrtc://@:8554/output \
  --threshold=0.8 \
  --recognition-interval=0.75
```

## Labeling Unknowns
The dashboard sends a websocket message to `/camera/label_face` with:
```json
{"unknown_id": "...", "name": "Zhuo Le"}
```
The service stores the image under `data/Faces/Zhuo Le/` and updates the database.

## Notes
- `UNKNOWN` is emitted when no match is found.
- Visit tracking uses a 5-minute interaction window, like the doorbell demo.
