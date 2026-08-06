#!/usr/bin/env python3
"""Compile and run one vendored Rust Quiz question."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit("usage: adapter.py CASE SOURCE EXPECTED LIBSTD_TAR STAMP")
    case, source, expected_path, libstd_tar, stamp = sys.argv[1:]
    source = os.path.abspath(source)
    expected_path = os.path.abspath(expected_path)
    libstd_tar = os.path.abspath(libstd_tar)
    stamp = os.path.abspath(stamp)
    rustc = lib.require_env("RUSTC")

    environment = dict(os.environ)
    environment["MRUSTC_TARGET_VER"] = "1.90"
    environment.setdefault("CC", "cc")
    print(f"[Rust Quiz] {case}", file=sys.stderr, flush=True)

    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        binary = os.path.join(work, "test")
        compile_result = subprocess.run(
            [
                rustc,
                source,
                "-L", os.path.join(libstd, "release"),
                "-o", binary,
                "--crate-type", "bin",
                "--edition", "2021",
            ],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if compile_result.returncode != 0:
            sys.stdout.buffer.write(compile_result.stdout)
            sys.stderr.buffer.write(compile_result.stderr)
            return compile_result.returncode

        try:
            run_result = subprocess.run(
                [binary],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"FAIL {case}: timed out after 30 seconds", file=sys.stderr)
            return 1

        if run_result.returncode != 0:
            sys.stdout.buffer.write(run_result.stdout)
            sys.stderr.buffer.write(run_result.stderr)
            print(f"FAIL {case}: exit {run_result.returncode}", file=sys.stderr)
            return run_result.returncode or 1

        expected = open(expected_path, "rb").read()
        if run_result.stdout != expected:
            print(f"FAIL {case}: stdout differs from {expected_path}", file=sys.stderr)
            sys.stderr.buffer.write(b"actual:\n" + run_result.stdout)
            return 1

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
