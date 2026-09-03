#!/usr/bin/env python3
"""Require Fine's primary README program to equal the shipped playground fixture."""

from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
readme = (root / "fine" / "README.md").read_text()
fixture = (root / "fine" / "fixtures" / "playground-demo.fine").read_text()
if fixture.endswith("\n"):
    fixture = fixture[:-1]
start = "<!-- checked-example: playground-demo -->\n```fine\n"
end = "\n```\n<!-- /checked-example -->"
if readme.count(start) != 1:
    raise SystemExit("README must contain exactly one checked playground-demo block")
body = readme.split(start, 1)[1].split(end, 1)[0]
if body != fixture:
    raise SystemExit("README playground demo differs from fine/fixtures/playground-demo.fine")
print("README playground demo matches the executable fixture")
