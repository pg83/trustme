#!/usr/bin/env python3
"""Compile and execute one shard of Rust Reference code fences."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def fail_output(result: subprocess.CompletedProcess, case: str, stage: str) -> int:
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    print(f"FAIL Rust Reference {case}: {stage} exit {result.returncode}", file=sys.stderr)
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
        library_path = os.path.join(libstd, "release")
        for index, (case, origin, edition, mode) in enumerate(selected):
            print(f"[Rust Reference {mode}] {origin}", file=sys.stderr, flush=True)
            source = os.path.join(upstream, case)
            binary = os.path.join(work, f"test-{index}")
            compile_result = subprocess.run(
                lib.wrap_gdb([
                    rustc,
                    source,
                    "-L", library_path,
                    "-o", binary,
                    "--crate-type", "bin",
                    "--edition", edition,
                ]),
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if mode == "fail":
                if compile_result.returncode == 0:
                    return fail_output(compile_result, case, "unexpected compile success")
                continue
            if mode == "xfail":
                # A rejection this compiler does not make, and is not going to
                # (see XFAIL.md). Recorded rather than dropped, so that making
                # it starts failing here and the row can move back to `fail`.
                if compile_result.returncode == 0:
                    continue
                return fail_output(compile_result, case, "compiled as expected once, now rejected (move to `fail`)")
            if compile_result.returncode != 0:
                return fail_output(compile_result, case, "compile")
            if mode == "compile":
                continue
            if mode not in ("pass", "panic"):
                raise SystemExit(f"unknown Rust Reference mode {mode!r} for {case}")
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
                print(f"FAIL Rust Reference {case}: timed out after 60 seconds", file=sys.stderr)
                return 1
            success = run_result.returncode == 0 if mode == "pass" else run_result.returncode != 0
            if not success:
                return fail_output(run_result, case, "runtime")

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
