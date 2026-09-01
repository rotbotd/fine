#!/usr/bin/env python3
"""Validate one Fine source snapshot against a Rainfall JSONL replay."""

from __future__ import annotations

import argparse
from pathlib import Path
from rainfall_replay import ValidationError, load_events, validate


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("rainfall", type=Path)
    arguments = parser.parse_args()
    try:
        with arguments.rainfall.open() as stream:
            result = validate(arguments.source.read_bytes(), load_events(stream))
    except (OSError, ValidationError) as error:
        parser.exit(1, f"fine-rain-validate: {error}\n")
    print("valid rainfall: " + ", ".join(f"{key}={value}" for key, value in result.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
