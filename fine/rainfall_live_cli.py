#!/usr/bin/env python3
"""Command-line entry point for the local Rainfall browser editor."""

from __future__ import annotations

import argparse
from pathlib import Path

from rainfall_live import LiveError, LiveServer, initialize_session, make_handler


def main() -> int:
    parser = argparse.ArgumentParser(description="local live editor for Fine Rainfall evidence")
    parser.add_argument("host", type=Path, help="persistent Rainfall host directory")
    parser.add_argument("source", type=Path, help="initial Fine source")
    parser.add_argument("--fine", type=Path,
                        help="Fine executable (defaults to the installed sibling)")
    parser.add_argument("--document")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--port", type=int, default=8731)
    arguments = parser.parse_args()
    try:
        fine = arguments.fine or Path(__file__).resolve().with_name("fine")
        session = initialize_session(arguments.host, arguments.source, fine,
                                     arguments.document, arguments.resume)
        server = LiveServer(("127.0.0.1", arguments.port), make_handler(session))
    except (OSError, LiveError, ValueError) as error:
        parser.exit(1, f"fine-rain-live: {error}\n")
    address, port = server.server_address[:2]
    print(f"fine-rain-live: http://{address}:{port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
