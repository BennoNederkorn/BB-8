import os
import tempfile

import numpy as np

from face_service.face_db import FaceDatabase


def main() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        faces_root = os.path.join(tmpdir, "Faces")
        db_path = os.path.join(tmpdir, "face_db.sqlite3")
        db = FaceDatabase(db_path, faces_root)
        person_id = db.get_or_create_person("Test User")
        encoding = np.zeros(128, dtype=np.float64)
        db.add_encoding(person_id, encoding, image_path=None)
        known = db.get_known_encodings()
        print("known_count", len(known))
        db.close()


if __name__ == "__main__":
    main()
