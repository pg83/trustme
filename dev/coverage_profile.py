#!/usr/bin/env python3
"""Collapse the process profiles of one test node into a declared output."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("profiles", nargs="+", type=Path)
    args = parser.parse_args()

    profiles = sorted(
        profile
        for directory in args.profiles
        for profile in directory.glob("*.profraw")
    )
    if not profiles:
        # Style and driver-only nodes execute no instrumented binary. Keep an
        # empty declared output; the final merge skips it.
        args.output.write_bytes(b"")
        return
    subprocess.run([
        "llvm-profdata", "merge", "-sparse", *map(str, profiles),
        "-o", str(args.output),
    ], check=True)


if __name__ == "__main__":
    main()
