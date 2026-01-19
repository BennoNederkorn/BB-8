import threading
from typing import Any, Dict, List, Optional

import cv2
import numpy as np
import face_recognition

from .face_db import FaceDatabase


class FaceRecognizer:
    def __init__(self, db: FaceDatabase, tolerance: float = 0.65, model: str = "hog") -> None:
        self._db = db
        self._tolerance = tolerance
        self._model = model
        self._lock = threading.Lock()
        self._known_encodings: List[np.ndarray] = []
        self._known_person_ids: List[int] = []
        self._known_names: List[str] = []

    def load_known_faces(self) -> None:
        entries = self._db.get_known_encodings()
        with self._lock:
            self._known_encodings = [entry["encoding"] for entry in entries]
            self._known_person_ids = [int(entry["person_id"]) for entry in entries]
            self._known_names = [entry["name"] for entry in entries]

    def sync_from_faces_dir(self) -> int:
        added = 0
        for name, image_path in self._db.list_face_images():
            if self._db.encoding_exists(image_path):
                continue
            image = face_recognition.load_image_file(image_path)
            encodings = face_recognition.face_encodings(image)
            if len(encodings) == 0:
                continue
            person_id = self._db.get_or_create_person(name)
            self._db.add_encoding(person_id, encodings[0], image_path)
            added += 1
        if added > 0:
            self.load_known_faces()
        return added

    def encode_face(self, face_bgr: np.ndarray, scale: float = 0.5) -> Optional[np.ndarray]:
        if scale <= 0:
            return None
        if scale != 1.0:
            face_bgr = cv2.resize(face_bgr, (0, 0), fx=scale, fy=scale)
        face_rgb = cv2.cvtColor(face_bgr, cv2.COLOR_BGR2RGB)
        encodings = face_recognition.face_encodings(face_rgb, model=self._model)
        if len(encodings) == 0:
            return None
        return encodings[0]

    def match(self, face_encoding: np.ndarray) -> Optional[Dict[str, Any]]:
        with self._lock:
            if not self._known_encodings:
                return None
            distances = face_recognition.face_distance(self._known_encodings, face_encoding)
            best_index = int(np.argmin(distances))
            best_distance = float(distances[best_index])
            if best_distance < self._tolerance:
                return {
                    "person_id": self._known_person_ids[best_index],
                    "name": self._known_names[best_index],
                    "distance": best_distance,
                }
        return None

    def register_new_encoding(self, person_id: int, name: str, encoding: np.ndarray) -> None:
        with self._lock:
            self._known_encodings.append(np.asarray(encoding, dtype=np.float64))
            self._known_person_ids.append(person_id)
            self._known_names.append(name)
