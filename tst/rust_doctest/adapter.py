#!/usr/bin/env python3
"""Compile and run one extracted Rust 1.90 library doctest."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402
import program  # noqa: E402


def main() -> int:
    if len(sys.argv) != 7:
        raise SystemExit("usage: adapter.py CASE SOURCE EDITION MODE LIBSTD_TAR STAMP")
    case, source, edition, mode, libstd_tar, stamp = sys.argv[1:]
    source = os.path.abspath(source)
    libstd_tar = os.path.abspath(libstd_tar)
    stamp = os.path.abspath(stamp)
    rustc = lib.require_env("RUSTC")
    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    print(f"[Rust doctest] {case}", file=sys.stderr, flush=True)

    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        binary = os.path.join(work, "test")
        compile_result = subprocess.run(
            lib.wrap_gdb([
                rustc,
                source,
                "-L", os.path.join(libstd, "release"),
                "-o", binary,
                "--crate-type", "bin",
                "--edition", edition,
            ]),
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
            run_result = program.run(
                [binary],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=60,
            )
        except subprocess.TimeoutExpired:
            print(f"FAIL {case}: timed out after 60 seconds", file=sys.stderr)
            return 1

        if mode == "panic":
            success = run_result.returncode != 0
        else:
            success = run_result.returncode == 0
        if not success:
            sys.stdout.buffer.write(run_result.stdout)
            sys.stderr.buffer.write(run_result.stderr)
            print(f"FAIL {case}: exit {run_result.returncode}, mode {mode}", file=sys.stderr)
            return 1

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
