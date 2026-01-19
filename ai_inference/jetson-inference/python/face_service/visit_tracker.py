from datetime import timedelta
from typing import Any, Dict

from .face_db import FaceDatabase


class VisitTracker:
    def __init__(self, db: FaceDatabase, interaction_window: timedelta) -> None:
        self._db = db
        self._interaction_window = interaction_window

    def update(self, person_id: int) -> Dict[str, Any]:
        return self._db.update_visit(person_id, self._interaction_window)
