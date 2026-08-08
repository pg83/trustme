#!/usr/bin/env python3
"""Verify that the compiler defaults to the supported Rust language version."""

import os
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_target_version_default.py RUSTC STAMP")

    rustc, stamp = sys.argv[1:]
    env = dict(os.environ)
    env.pop("MRUSTC_TARGET_VER", None)
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
            "rustc without MRUSTC_TARGET_VER must default to 1.90; got "
            + result.stdout.splitlines()[0]
        )

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
