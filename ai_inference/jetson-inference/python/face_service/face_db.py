import os
import sqlite3
import threading
import uuid
from datetime import datetime, timedelta
from typing import Any, Dict, Iterable, List, Optional, Tuple

import cv2
import numpy as np


_TIMESTAMP_FORMAT = "%Y-%m-%dT%H:%M:%SZ"


def _utcnow() -> datetime:
    return datetime.utcnow()


def _format_ts(dt: datetime) -> str:
    return dt.strftime(_TIMESTAMP_FORMAT)


def _parse_ts(value: str) -> datetime:
    return datetime.strptime(value, _TIMESTAMP_FORMAT)


class FaceDatabase:
    def __init__(self, db_path: str, faces_root: str) -> None:
        self.db_path = db_path
        self.faces_root = faces_root
        self.unknown_root = os.path.join(faces_root, "_unknown")
        self._lock = threading.Lock()
        self._conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._ensure_paths()
        self._ensure_schema()

    def _ensure_paths(self) -> None:
        os.makedirs(self.faces_root, exist_ok=True)
        os.makedirs(self.unknown_root, exist_ok=True)

    def _ensure_schema(self) -> None:
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute(
                """
                CREATE TABLE IF NOT EXISTS persons (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    name TEXT UNIQUE NOT NULL,
                    created_at TEXT NOT NULL
                )
                """
            )
            cursor.execute(
                """
                CREATE TABLE IF NOT EXISTS encodings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    person_id INTEGER NOT NULL,
                    encoding BLOB NOT NULL,
                    image_path TEXT,
                    created_at TEXT NOT NULL,
                    FOREIGN KEY(person_id) REFERENCES persons(id)
                )
                """
            )
            cursor.execute(
                """
                CREATE TABLE IF NOT EXISTS visits (
                    person_id INTEGER PRIMARY KEY,
                    first_seen TEXT NOT NULL,
                    first_seen_this_interaction TEXT NOT NULL,
                    last_seen TEXT NOT NULL,
                    seen_count INTEGER NOT NULL,
                    seen_frames INTEGER NOT NULL,
                    FOREIGN KEY(person_id) REFERENCES persons(id)
                )
                """
            )
            cursor.execute(
                """
                CREATE TABLE IF NOT EXISTS unknown_faces (
                    id TEXT PRIMARY KEY,
                    encoding BLOB NOT NULL,
                    image_path TEXT NOT NULL,
                    created_at TEXT NOT NULL
                )
                """
            )
            self._conn.commit()

    def close(self) -> None:
        with self._lock:
            self._conn.close()

    def get_or_create_person(self, name: str) -> int:
        now = _format_ts(_utcnow())
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute("SELECT id FROM persons WHERE name = ?", (name,))
            row = cursor.fetchone()
            if row is not None:
                return int(row["id"])
            cursor.execute(
                "INSERT INTO persons (name, created_at) VALUES (?, ?)",
                (name, now),
            )
            self._conn.commit()
            return int(cursor.lastrowid)

    def add_encoding(self, person_id: int, encoding: np.ndarray, image_path: Optional[str] = None) -> None:
        encoded = np.asarray(encoding, dtype=np.float64).tobytes()
        now = _format_ts(_utcnow())
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute(
                "INSERT INTO encodings (person_id, encoding, image_path, created_at) VALUES (?, ?, ?, ?)",
                (person_id, encoded, image_path, now),
            )
            self._conn.commit()

    def encoding_exists(self, image_path: str) -> bool:
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute("SELECT 1 FROM encodings WHERE image_path = ?", (image_path,))
            return cursor.fetchone() is not None

    def get_known_encodings(self) -> List[Dict[str, Any]]:
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute(
                """
                SELECT encodings.person_id, encodings.encoding, persons.name
                FROM encodings
                JOIN persons ON persons.id = encodings.person_id
                """
            )
            rows = cursor.fetchall()
        result: List[Dict[str, Any]] = []
        for row in rows:
            encoding = np.frombuffer(row["encoding"], dtype=np.float64)
            result.append({
                "person_id": int(row["person_id"]),
                "name": row["name"],
                "encoding": encoding,
            })
        return result

    def update_visit(self, person_id: int, interaction_window: timedelta) -> Dict[str, Any]:
        now = _utcnow()
        now_str = _format_ts(now)
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute("SELECT * FROM visits WHERE person_id = ?", (person_id,))
            row = cursor.fetchone()
            if row is None:
                cursor.execute(
                    """
                    INSERT INTO visits (person_id, first_seen, first_seen_this_interaction, last_seen, seen_count, seen_frames)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """,
                    (person_id, now_str, now_str, now_str, 1, 1),
                )
                self._conn.commit()
                return {
                    "first_seen": now,
                    "first_seen_this_interaction": now,
                    "last_seen": now,
                    "seen_count": 1,
                    "seen_frames": 1,
                    "seconds_at_door": 0,
                }

            first_seen = _parse_ts(row["first_seen"])
            first_seen_this = _parse_ts(row["first_seen_this_interaction"])
            last_seen = _parse_ts(row["last_seen"])
            seen_count = int(row["seen_count"])
            seen_frames = int(row["seen_frames"]) + 1

            if now - first_seen_this > interaction_window:
                first_seen_this = now
                seen_count += 1

            cursor.execute(
                """
                UPDATE visits
                SET first_seen_this_interaction = ?, last_seen = ?, seen_count = ?, seen_frames = ?
                WHERE person_id = ?
                """,
                (_format_ts(first_seen_this), now_str, seen_count, seen_frames, person_id),
            )
            self._conn.commit()

        seconds_at_door = int((now - first_seen_this).total_seconds())
        return {
            "first_seen": first_seen,
            "first_seen_this_interaction": first_seen_this,
            "last_seen": now,
            "seen_count": seen_count,
            "seen_frames": seen_frames,
            "seconds_at_door": seconds_at_door,
        }

    def add_unknown_face(self, encoding: np.ndarray, face_bgr: np.ndarray) -> str:
        unknown_id = str(uuid.uuid4())
        filename = f"{unknown_id}.jpg"
        image_path = os.path.join(self.unknown_root, filename)
        os.makedirs(self.unknown_root, exist_ok=True)
        cv2.imwrite(image_path, face_bgr)

        encoded = np.asarray(encoding, dtype=np.float64).tobytes()
        now = _format_ts(_utcnow())
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute(
                "INSERT INTO unknown_faces (id, encoding, image_path, created_at) VALUES (?, ?, ?, ?)",
                (unknown_id, encoded, image_path, now),
            )
            self._conn.commit()
        return unknown_id

    def label_unknown(self, unknown_id: str, name: str) -> Optional[int]:
        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute("SELECT * FROM unknown_faces WHERE id = ?", (unknown_id,))
            row = cursor.fetchone()
            if row is None:
                return None

        person_id = self.get_or_create_person(name)
        encoding = np.frombuffer(row["encoding"], dtype=np.float64)
        image_path = row["image_path"]
        target_dir = os.path.join(self.faces_root, name)
        os.makedirs(target_dir, exist_ok=True)
        target_path = os.path.join(target_dir, f"{name}_{unknown_id}.jpg")
        try:
            os.replace(image_path, target_path)
        except Exception:
            target_path = image_path

        self.add_encoding(person_id, encoding, target_path)

        with self._lock:
            cursor = self._conn.cursor()
            cursor.execute("DELETE FROM unknown_faces WHERE id = ?", (unknown_id,))
            self._conn.commit()
        return person_id

    def list_face_images(self) -> Iterable[Tuple[str, str]]:
        if not os.path.isdir(self.faces_root):
            return []
        entries: List[Tuple[str, str]] = []
        for name in os.listdir(self.faces_root):
            if name.startswith("_"):
                continue
            person_dir = os.path.join(self.faces_root, name)
            if not os.path.isdir(person_dir):
                continue
            for filename in os.listdir(person_dir):
                if not filename.lower().endswith((".jpg", ".jpeg", ".png")):
                    continue
                entries.append((name, os.path.join(person_dir, filename)))
        return entries
