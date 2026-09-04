#!/usr/bin/env python3
"""Require public Fine examples to be literal excerpts of passing fixtures."""

from pathlib import Path
import re
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

fixture_sources = {
    path.name: path.read_text()
    for path in sorted((root / "fine" / "fixtures").glob("*.fine"))
    if not path.name.startswith("reject-")
}
documents = [
    root / "fine" / "README.md",
    root / "fine" / "ARCHITECTURE.md",
    root / "fine" / "PROOF_TERMS.md",
    root / "fine" / "ROADMAP.md",
]
checked = 0
for document in documents:
    blocks = re.findall(r"```fine\n(.*?)\n```", document.read_text(), re.DOTALL)
    if not blocks:
        raise SystemExit(f"{document.relative_to(root)} contains no checked Fine examples")
    for number, block in enumerate(blocks, 1):
        owners = [name for name, source in fixture_sources.items() if block in source]
        if not owners:
            raise SystemExit(
                f"{document.relative_to(root)} Fine block {number} is not an exact excerpt "
                "of a passing fixture"
            )
        checked += 1

print(f"checked {checked} public Fine examples against passing fixtures")
