#!/usr/bin/env python3
"""Compile one shard of positive gccrs compile tests."""

import os
import platform
import re
import shlex
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def directives(name: str, text: str) -> list[str]:
    return re.findall(
        rf'\{{\s*dg-{re.escape(name)}\s+"((?:[^"\\]|\\.)*)"',
        text,
    )


def compiler_options(text: str) -> list[str]:
    result = []
    for value in directives("options", text) + directives("additional-options", text):
        for option in shlex.split(value):
            if re.fullmatch(r"-O[0-3s]?", option):
                if "-O" not in result and option != "-O0":
                    result.append("-O")
            elif option.startswith("-frust-cfg="):
                result.extend(["--cfg", option.removeprefix("-frust-cfg=")])
    return result


def target_applies(text: str) -> bool:
    machine = platform.machine().lower()
    targets = re.findall(r"dg-do\s+compile\s+\{\s*target\s+([^\s}]+)", text)
    for target in targets:
        if target.startswith("x86_64") and machine not in ("x86_64", "amd64"):
            return False
        if target.startswith("arm") and not machine.startswith("arm"):
            return False
    if "dg-require-effective-target lp64" in text and struct.calcsize("P") != 8:
        return False
    return True


def fail_output(result: subprocess.CompletedProcess, case: str) -> int:
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    print(f"FAIL gccrs compile {case}: exit {result.returncode}", file=sys.stderr)
    return result.returncode or 1


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit("usage: adapter.py CASES START COUNT UPSTREAM STAMP")
    cases_path, start_text, count_text, upstream, stamp = sys.argv[1:]
    start = int(start_text)
    count = int(count_text)
    upstream = os.path.abspath(upstream)
    stamp = os.path.abspath(stamp)
    rustc = lib.require_env("RUSTC")
    cases = open(cases_path).read().splitlines()
    selected = cases[start:start + count]
    if len(selected) != count:
        raise SystemExit(f"manifest contains no complete shard at row {start}")

    base_environment = dict(os.environ)
    base_environment["MRUSTC_TARGET_VER"] = "1.90"
    base_environment.setdefault("CC", "cc")
    with lib.workdir() as work:
        for index, case in enumerate(selected):
            print(f"[gccrs compile] {case}", file=sys.stderr, flush=True)
            source = os.path.join(upstream, case)
            text = open(source, encoding="utf-8", errors="surrogateescape").read()
            if not target_applies(text):
                print(f"SKIP {case}: target directive does not apply", file=sys.stderr)
                continue
            environment = dict(base_environment)
            for key, value in re.findall(
                r'\{\s*dg-set-compiler-env-var\s+([^\s]+)\s+"([^"]*)"', text
            ):
                environment[key] = value
            compile_result = subprocess.run(
                [
                    rustc,
                    source,
                    "-o", os.path.join(work, f"case-{index}.rlib"),
                    "--crate-type", "lib",
                    "--edition", "2015",
                    *compiler_options(text),
                ],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if compile_result.returncode != 0:
                return fail_output(compile_result, case)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
