#!/usr/bin/env python3
"""Compile and run one vendored Rust 1.90 run-pass test."""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def normalized(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n")


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit("usage: adapter.py CASE SOURCE LIBSTD_TAR STAMP")
    case, source, libstd_tar, stamp = sys.argv[1:]
    source = os.path.abspath(source)
    libstd_tar = os.path.abspath(libstd_tar)
    stamp = os.path.abspath(stamp)
    rustc = lib.require_env("RUSTC")
    text = open(source, encoding="utf-8", errors="surrogateescape").read()

    edition_match = re.search(r"^//@\s*edition:\s*(\d+)", text, re.MULTILINE)
    edition = edition_match.group(1) if edition_match else "2015"
    compile_flags = []
    for value in re.findall(r"^//@\s*compile-flags:\s*(.*)$", text,
                            re.MULTILINE):
        compile_flags.extend(lib.compiletest_split_flags(value))
    run_flags = []
    for value in re.findall(r"^//@\s*run-flags:\s*(.*)$", text,
                            re.MULTILINE):
        run_flags.extend(lib.compiletest_split_flags(value))
    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    for key, value in re.findall(r"^//@\s*rustc-env:([^=\s]+)=(.*)$", text, re.MULTILINE):
        environment[key] = value

    print(f"[rust-1.90 run-pass] {case}", file=sys.stderr, flush=True)
    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        binary = os.path.join(work, "test")
        compile_result = subprocess.run(
            [
                rustc,
                source,
                "-L", os.path.join(libstd, "release"),
                *compile_flags,
                "-o", binary,
                "--crate-type", "bin",
                "--edition", edition,
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
                [binary, *run_flags],
                env=environment,
                cwd=work,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=60,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"FAIL {case}: timed out after 60 seconds", file=sys.stderr)
            return 1

        if run_result.returncode != 0:
            sys.stdout.buffer.write(run_result.stdout)
            sys.stderr.buffer.write(run_result.stderr)
            print(f"FAIL {case}: exit {run_result.returncode}", file=sys.stderr)
            return run_result.returncode or 1

        expected_base = os.path.splitext(source)[0] + ".run"
        for stream, actual in (("stdout", run_result.stdout), ("stderr", run_result.stderr)):
            expected_path = expected_base + "." + stream
            if not os.path.exists(expected_path):
                continue
            expected = open(expected_path, "rb").read()
            if normalized(actual) != normalized(expected):
                print(f"FAIL {case}: {stream} differs from {expected_path}", file=sys.stderr)
                sys.stderr.buffer.write(b"actual:\n" + actual)
                return 1

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
