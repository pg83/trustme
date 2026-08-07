#!/usr/bin/env python3
"""Compile and run one tests/unit/test_*.rs against a prebuilt libstd, then write
a stamp. Each unit test is its own graph node — a self-contained regression for
one compiler fix that must compile and exit 0.

    run_one.py <test.rs> <libstd.tar> <stamp>

Environment: RUSTC, CC.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    src = os.path.abspath(sys.argv[1])
    libstd_tar = os.path.abspath(sys.argv[2])
    stamp = os.path.abspath(sys.argv[3])
    rustc = lib.require_env("RUSTC")
    source_text = open(src, encoding="utf-8", errors="surrogateescape").read()
    test_harness = "//@ test-harness" in source_text

    with lib.workdir() as work:
        env = dict(os.environ)
        env["MRUSTC_TARGET_VER"] = "1.90"
        env.setdefault("CC", "cc")

        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        binary = os.path.join(work, "t")
        mode = ["--test"] if test_harness else ["--crate-type", "bin"]
        lib.run([rustc, src, "-L", os.path.join(libstd, "release"), "-o", binary,
                 *mode, "--edition", "2021"], env=env)
        if test_harness:
            listing = subprocess.run([binary, "--list"], env=env,
                                     stdout=subprocess.PIPE, timeout=60,
                                     check=True)
            if b": test" not in listing.stdout:
                raise RuntimeError("test harness contains no tests")
        lib.run([binary], env=env, timeout=60)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
