#!/usr/bin/env python3
"""Run one exact test from a grouped Rust library-test harness."""

import os
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit("usage: run.py CASE FUNCTION HINT HARNESS STAMP")
    case, function, hint, harness, stamp = sys.argv[1:]
    harness = os.path.abspath(harness)
    stamp = os.path.abspath(stamp)
    print(f"[Rust library test] {case}", file=sys.stderr, flush=True)

    listing = subprocess.run(
        [harness, "--list"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=60,
        check=False,
    )
    if listing.returncode != 0:
        sys.stdout.buffer.write(listing.stdout)
        sys.stderr.buffer.write(listing.stderr)
        return listing.returncode or 1

    names = []
    for line in listing.stdout.decode("utf-8", errors="surrogateescape").splitlines():
        if line.endswith(": test"):
            raw_name = line.removesuffix(": test")
            names.append((raw_name.removeprefix("::"), raw_name))
    candidates = [
        (name, raw_name)
        for name, raw_name in names
        if name.rsplit("::", 1)[-1] == function
    ]
    exact = [raw_name for name, raw_name in candidates if name == hint]
    if len(exact) == 1:
        selected = exact[0]
    elif len(candidates) == 1:
        selected = candidates[0][1]
    else:
        print(
            f"FAIL {case}: expected one harness test for {function}, got {candidates}",
            file=sys.stderr,
        )
        return 1

    try:
        result = subprocess.run(
            [harness, selected, "--exact", "--include-ignored", "--nocapture"],
            timeout=60,
            check=False,
        )
    except subprocess.TimeoutExpired:
        print(f"FAIL {case}: timed out after 60 seconds", file=sys.stderr)
        return 1
    if result.returncode != 0:
        return result.returncode or 1

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
