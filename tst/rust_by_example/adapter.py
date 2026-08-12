#!/usr/bin/env python3
"""Compile and run one shard of extracted Rust By Example programs."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def fail_output(result: subprocess.CompletedProcess, case: str, stage: str) -> int:
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    print(f"FAIL Rust By Example {case}: {stage} exit {result.returncode}", file=sys.stderr)
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

    cases = [line.split("\t") for line in open(cases_path).read().splitlines()]
    selected = cases[start:start + count]
    if len(selected) != count:
        raise SystemExit(f"manifest contains no complete shard at row {start}")

    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        for index, (case, origin, edition) in enumerate(selected):
            print(f"[Rust By Example] {origin}", file=sys.stderr, flush=True)
            source = os.path.join(upstream, case)
            binary = os.path.join(work, f"test-{index}")
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
                return fail_output(compile_result, case, "compile")
            try:
                run_result = subprocess.run(
                    [binary],
                    cwd=work,
                    env=environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=60,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                print(f"FAIL Rust By Example {case}: timed out after 60 seconds", file=sys.stderr)
                return 1
            if run_result.returncode != 0:
                return fail_output(run_result, case, "runtime")

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
