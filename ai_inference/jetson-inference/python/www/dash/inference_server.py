#!/usr/bin/env python3
"""Legacy entrypoint. Use face_service.inference_server instead."""

from face_service.inference_server import main


if __name__ == "__main__":
    main()
