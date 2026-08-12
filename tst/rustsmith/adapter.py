#!/usr/bin/env python3
"""Compile and run one ten-program RustSmith corpus shard."""

import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def fail_output(result: subprocess.CompletedProcess, case: str, stage: str) -> int:
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    print(f"FAIL RustSmith seed {case}: {stage} exit {result.returncode}", file=sys.stderr)
    return result.returncode or 1


def main() -> int:
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: adapter.py CASES START COUNT UPSTREAM LIBSTD_TAR STAMP"
        )
    cases_path, start_text, count_text, upstream, libstd_tar, stamp = sys.argv[1:]
    start = int(start_text)
    count = int(count_text)
    upstream = os.path.abspath(upstream)
    libstd_tar = os.path.abspath(libstd_tar)
    stamp = os.path.abspath(stamp)
    rustc = lib.require_env("RUSTC")

    rows = [line.split("\t") for line in open(cases_path).read().splitlines()]
    selected = rows[start:start + count]
    if len(selected) != count:
        raise SystemExit(f"manifest contains no complete shard at row {start}")

    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        for stem, seed in selected:
            print(f"[RustSmith] seed {seed}", file=sys.stderr, flush=True)
            source = os.path.join(upstream, stem + ".rs")
            binary = os.path.join(work, "test-" + stem)
            compile_result = subprocess.run(
                lib.wrap_gdb([
                    rustc,
                    source,
                    "-L", os.path.join(libstd, "release"),
                    "-o", binary,
                    "--crate-type", "bin",
                    "--edition", "2021",
                    "-O",
                ]),
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if compile_result.returncode != 0:
                return fail_output(compile_result, seed, "compile")

            arguments = shlex.split(
                open(os.path.join(upstream, stem + ".args")).read()
            )
            try:
                run_result = subprocess.run(
                    [binary, *arguments],
                    env=environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=60,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                print(f"FAIL RustSmith seed {seed}: timed out after 60 seconds", file=sys.stderr)
                return 1
            if run_result.returncode != 0:
                return fail_output(run_result, seed, "runtime")

            expected = open(os.path.join(upstream, stem + ".stdout"), "rb").read()
            if run_result.stdout != expected:
                print(f"FAIL RustSmith seed {seed}: stdout differs", file=sys.stderr)
                sys.stderr.buffer.write(b"expected:\n" + expected)
                sys.stderr.buffer.write(b"actual:\n" + run_result.stdout)
                return 1

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
