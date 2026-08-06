#!/usr/bin/env python3
"""Compile and run one tests/unit/test_*.rs against a prebuilt libstd, then write
a stamp. Each unit test is its own graph node — a self-contained regression for
one compiler fix that must compile and exit 0.

    run_one.py <test.rs> <libstd.tar> <stamp>

Environment: RUSTC, CC.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    src = os.path.abspath(sys.argv[1])
    libstd_tar = os.path.abspath(sys.argv[2])
    stamp = os.path.abspath(sys.argv[3])
    rustc = lib.require_env("RUSTC")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["MRUSTC_TARGET_VER"] = "1.90"
        env.setdefault("CC", "cc")

        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        binary = os.path.join(work, "t")
        lib.run([rustc, src, "-L", os.path.join(libstd, "release"), "-o", binary,
                 "--crate-type", "bin", "--edition", "2021"], env=env)
        lib.run([binary], env=env)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
