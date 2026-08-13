#!/usr/bin/env python3
"""Compile one shard of positive Rust 1.90 UI compile tests."""

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def fail_output(result: subprocess.CompletedProcess, case: str) -> int:
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    print(f"FAIL Rust UI compile {case}: exit {result.returncode}", file=sys.stderr)
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
    cases = json.load(open(cases_path))
    selected = cases[start:start + count]
    if len(selected) != count:
        raise SystemExit(f"manifest contains no complete shard at row {start}")

    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    system_rustc = os.environ.get("TRUSTME_SYSTEM_RUSTC") == "1"
    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        library_path = os.path.join(libstd, "release")
        for index, case in enumerate(selected):
            relative = case["path"]
            print(
                f"[Rust UI {case['mode']}-pass] {relative}",
                file=sys.stderr,
                flush=True,
            )
            command = [
                rustc,
                os.path.join(upstream, relative),
                "-L", library_path,
                *lib.mrustc_compile_flags(
                    case["flags"], system_rustc=system_rustc
                ),
                "--edition", case["edition"],
            ]
            if case["mode"] == "check":
                command.append("--emit=metadata")
            if case["crate_type"] != "flags":
                command.extend(["--crate-type", case["crate_type"]])
            has_output = any(
                flag == "-o" or flag.startswith("-o")
                for flag in case["flags"]
            )
            if not has_output:
                command.extend(["-o", os.path.join(work, f"case-{index}")])
            compile_result = subprocess.run(
                lib.wrap_gdb(command),
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if compile_result.returncode != 0:
                return fail_output(compile_result, relative)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
