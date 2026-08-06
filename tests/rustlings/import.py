#!/usr/bin/env python3
"""Import the official solved Rustlings exercises.

Usage: import.py /path/to/rustlings
"""

import shutil
import sys
import tomllib
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: import.py /path/to/rustlings")
    checkout = Path(sys.argv[1]).resolve()
    info_path = checkout / "rustlings-macros" / "info.toml"
    solutions = checkout / "solutions"
    if not info_path.is_file() or not solutions.is_dir():
        raise SystemExit(f"missing Rustlings sources under {checkout}")

    info = tomllib.loads(info_path.read_text())
    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)

    cases = []
    for exercise in info["exercises"]:
        name = exercise["name"]
        relative = Path(exercise["dir"]) / f"{name}.rs"
        source = solutions / relative
        if not source.is_file():
            raise SystemExit(f"missing solution: {source}")
        destination = UPSTREAM / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        mode = "test" if exercise.get("test", True) else "run"
        cases.append((relative.as_posix(), mode))

    (HERE / "cases.tsv").write_text(
        "".join(f"{relative}\t{mode}\n" for relative, mode in cases)
    )
    print(f"imported {len(cases)} solved Rustlings exercises")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
