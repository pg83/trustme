#!/usr/bin/env python3
"""Verify that the compiler has no runtime-selectable language version."""

import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_target_version_default.py RUSTC STAMP")

    rustc, stamp = sys.argv[1:]
    env = dict(os.environ)
    # This was the old compatibility switch. It must not downgrade the fixed
    # Rust 1.90 compiler even when inherited from an old build environment.
    env["TRUSTME_TARGET_VER"] = "1.74"
    result = subprocess.run(
        [rustc, "-vV"],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError("rustc -vV failed")
    if not result.stdout.startswith("rustc 1.90.100 "):
        raise RuntimeError(
            "TRUSTME_TARGET_VER must not change fixed Rust 1.90; got "
            + result.stdout.splitlines()[0]
        )

    rustc_sources = Path(__file__).parents[2] / "bin" / "rustc"
    compatibility_branches = []
    for source in sorted(rustc_sources.iterdir()):
        if source.suffix not in {".cpp", ".h", ".inc"}:
            continue
        for line_number, line in enumerate(source.read_text().splitlines(), 1):
            if "TARGETVER_" in line:
                compatibility_branches.append(
                    f"{source.relative_to(rustc_sources.parent.parent)}:{line_number}: {line.strip()}"
                )
    if compatibility_branches:
        raise RuntimeError(
            "fixed Rust 1.90 compiler must not retain target-version branches:\n"
            + "\n".join(compatibility_branches)
        )

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
