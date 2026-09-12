#!/usr/bin/env python3
"""Measure Fine's exact finite proof-family closure on a worst-case chain."""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any


def chain_source(size: int) -> str:
    if size < 2:
        raise ValueError("chain size must be at least two")
    states = "\n".join(f"  p{index}," for index in range(size))
    constructors = ["  reach_zero() -> Reach(p0);"]
    constructors.extend(
        f"  reach_{index}() takes [previous: Reach(p{index - 1})] -> Reach(p{index});"
        for index in range(1, size - 1)
    )
    constructors.append(
        f"  reach_stuck() takes [previous: Reach(p{size - 1})] -> Reach(p{size - 1});"
    )
    return f"""enum Phase {{
{states}
}}

proof inductive Reach(value: Phase) {{
{chr(10).join(constructors)}
}}

function eliminate_stuck() -> Bool
  takes [evidence: Reach(p{size - 1})]
{{
  match evidence {{
  }}
}}
"""


def finite_event(output: str) -> dict[str, Any]:
    events = [json.loads(line) for line in output.splitlines() if line.strip()]
    matches = [
        event["data"]
        for event in events
        if event.get("operation") == "proof.inductive.finite-inhabitation"
        and event.get("data", {}).get("family") == "Reach"
    ]
    if len(matches) != 1:
        raise ValueError(f"expected one Reach finite-inhabitation event, found {len(matches)}")
    return matches[0]


def run_once(executable: Path, source: Path, size: int) -> tuple[float, dict[str, Any]]:
    started = time.perf_counter()
    completed = subprocess.run(
        [str(executable), "rain", str(source)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    elapsed = time.perf_counter() - started
    if completed.returncode != 0:
        raise RuntimeError(
            f"{executable} failed for size {size} with status {completed.returncode}:\n{completed.stderr}"
        )
    event = finite_event(completed.stdout)
    expected = {
        "domain_states": size,
        "reachable_states": size - 1,
        "rounds": size,
        "least_fixed_point": True,
    }
    for field, value in expected.items():
        if event.get(field) != value:
            raise ValueError(f"size {size}: expected {field}={value!r}, got {event.get(field)!r}")
    return elapsed, event


def run_wasm(module_root: Path, sources: list[tuple[int, Path]]) -> list[dict[str, Any]]:
    node = shutil.which("node")
    if node is None:
        raise RuntimeError("node is required for --wasm-implementation")
    harness = r'''import { pathToFileURL } from "node:url";
import path from "node:path";
import { readFile } from "node:fs/promises";
import { performance } from "node:perf_hooks";

const [root, ...specifications] = process.argv.slice(2);
const createFine = (await import(pathToFileURL(path.join(root, "fine.mjs")))).default;
const stdout = [];
const stderr = [];
const fine = await createFine({
  locateFile(file) { return path.join(root, file); },
  print(line) { stdout.push(line); },
  printErr(line) { stderr.push(line); },
});
const measurements = [];
for (const specification of specifications) {
  const separator = specification.indexOf(":");
  const size = Number(specification.slice(0, separator));
  const sourcePath = specification.slice(separator + 1);
  const virtualPath = `/finite-chain-${size}.fine`;
  fine.FS.writeFile(virtualPath, await readFile(sourcePath, "utf8"));
  stdout.length = 0;
  stderr.length = 0;
  const started = performance.now();
  let code = 0;
  try {
    code = fine.callMain(["rain", virtualPath]) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number") code = error.status;
    else throw error;
  }
  const elapsed = (performance.now() - started) / 1000;
  if (code !== 0) throw new Error(`Fine exited ${code}: ${stderr.join("\n")}`);
  const events = stdout.map((line) => JSON.parse(line));
  const matches = events.filter((event) =>
    event.operation === "proof.inductive.finite-inhabitation" && event.data?.family === "Reach");
  if (matches.length !== 1) throw new Error(`expected one Reach event, found ${matches.length}`);
  measurements.push({ size, elapsed_seconds: elapsed, event: matches[0].data });
  fine.FS.unlink(virtualPath);
}
console.log(JSON.stringify(measurements));
'''
    with tempfile.NamedTemporaryFile("w", suffix=".mjs", delete=False) as file:
        file.write(harness)
        harness_path = Path(file.name)
    try:
        completed = subprocess.run(
            [node, str(harness_path), str(module_root),
             *(f"{size}:{source}" for size, source in sources)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    finally:
        harness_path.unlink()
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--implementation",
        action="append",
        required=True,
        metavar="LABEL=FINE",
        help="named Fine executable; may be repeated",
    )
    parser.add_argument(
        "--wasm-implementation",
        action="append",
        default=[],
        metavar="LABEL=DIRECTORY",
        help="named directory containing fine.mjs and fine.wasm; may be repeated",
    )
    parser.add_argument("--sizes", nargs="+", type=int, default=[8, 16, 32, 64])
    parser.add_argument("--repetitions", type=int, default=1)
    arguments = parser.parse_args()
    if arguments.repetitions < 1:
        parser.error("--repetitions must be positive")

    implementations: list[tuple[str, Path]] = []
    for item in arguments.implementation:
        if "=" not in item:
            parser.error(f"invalid implementation {item!r}; expected LABEL=FINE")
        label, raw_path = item.split("=", 1)
        path = Path(raw_path).resolve()
        if not label or not path.is_file():
            parser.error(f"invalid implementation {item!r}")
        implementations.append((label, path))

    wasm_implementations: list[tuple[str, Path]] = []
    for item in arguments.wasm_implementation:
        if "=" not in item:
            parser.error(f"invalid Wasm implementation {item!r}; expected LABEL=DIRECTORY")
        label, raw_path = item.split("=", 1)
        path = Path(raw_path).resolve()
        if not label or not (path / "fine.mjs").is_file() or not (path / "fine.wasm").is_file():
            parser.error(f"invalid Wasm implementation {item!r}")
        wasm_implementations.append((label, path))

    profiles: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="fine-finite-profile-") as directory:
        root = Path(directory)
        for label, executable in implementations:
            measurements = []
            for size in arguments.sizes:
                source = root / f"chain-{size}.fine"
                source.write_text(chain_source(size))
                trials: list[float] = []
                event: dict[str, Any] | None = None
                for _ in range(arguments.repetitions):
                    elapsed, current = run_once(executable, source, size)
                    trials.append(elapsed)
                    if event is not None and current != event:
                        raise ValueError(f"size {size}: Rainfall metadata changed between repetitions")
                    event = current
                assert event is not None
                measurements.append(
                    {
                        "domain_states": size,
                        "elapsed_seconds_median": round(statistics.median(trials), 6),
                        "elapsed_seconds_trials": [round(value, 6) for value in trials],
                        "reachable_states": event["reachable_states"],
                        "rounds": event["rounds"],
                        "solver_checks": event.get("solver_checks"),
                    }
                )
            profiles.append({"implementation": label, "measurements": measurements})

        source_paths: list[tuple[int, Path]] = []
        for size in arguments.sizes:
            source = root / f"chain-{size}.fine"
            source.write_text(chain_source(size))
            source_paths.append((size, source))
        for label, module_root in wasm_implementations:
            trials_by_size: dict[int, list[float]] = {size: [] for size in arguments.sizes}
            events: dict[int, dict[str, Any]] = {}
            for _ in range(arguments.repetitions):
                for measurement in run_wasm(module_root, source_paths):
                    size = measurement["size"]
                    event = measurement["event"]
                    expected = {
                        "domain_states": size,
                        "reachable_states": size - 1,
                        "rounds": size,
                        "least_fixed_point": True,
                    }
                    for field, value in expected.items():
                        if event.get(field) != value:
                            raise ValueError(
                                f"Wasm size {size}: expected {field}={value!r}, got {event.get(field)!r}"
                            )
                    if size in events and event != events[size]:
                        raise ValueError(f"Wasm size {size}: Rainfall metadata changed between repetitions")
                    events[size] = event
                    trials_by_size[size].append(measurement["elapsed_seconds"])
            profiles.append(
                {
                    "implementation": label,
                    "measurements": [
                        {
                            "domain_states": size,
                            "elapsed_seconds_median": round(statistics.median(trials_by_size[size]), 6),
                            "elapsed_seconds_trials": [round(value, 6) for value in trials_by_size[size]],
                            "reachable_states": events[size]["reachable_states"],
                            "rounds": events[size]["rounds"],
                            "solver_checks": events[size].get("solver_checks"),
                        }
                        for size in arguments.sizes
                    ],
                }
            )

    report = {
        "schema": "fine.finite-inhabitation-profile.v1",
        "fixture": "one base, a length-(N-1) chain, and one unreachable self-supported state",
        "profiles": profiles,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
