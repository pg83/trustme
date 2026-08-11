#!/usr/bin/env python3
"""Compile and run one vendored gccrs execute test."""

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
    values = directives("options", text) + directives("additional-options", text)
    for value in values:
        for option in shlex.split(value):
            if re.fullmatch(r"-O[0-3s]?", option):
                if "-O" not in result and option != "-O0":
                    result.append("-O")
            elif option.startswith("-frust-cfg="):
                result.extend(["--cfg", option.removeprefix("-frust-cfg=")])
            # The remaining options control gccrs diagnostics, dumps, or its
            # parser version.  They have no mrustc command-line equivalent.
    return result


def edition(text: str) -> str:
    values = re.findall(r"-frust-edition=(2015|2018|2021|2024)", text)
    return values[-1] if values else "2015"


def target_applies(text: str) -> bool:
    machine = platform.machine().lower()
    target_matches = re.findall(r"dg-do\s+run\s+\{\s*target\s+([^\s}]+)", text)
    for target in target_matches:
        if target.startswith("x86_64") and machine not in ("x86_64", "amd64"):
            return False
        if target.startswith("arm") and not machine.startswith("arm"):
            return False
    if "dg-require-effective-target lp64" in text and struct.calcsize("P") != 8:
        return False
    return True


def write_stamp(stamp: str) -> None:
    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit("usage: adapter.py CASE SOURCE LIBSTD_TAR STAMP")
    case, source, libstd_tar, stamp = sys.argv[1:]
    source = os.path.abspath(source)
    libstd_tar = os.path.abspath(libstd_tar)
    stamp = os.path.abspath(stamp)
    rustc = lib.require_env("RUSTC")
    text = open(source, encoding="utf-8", errors="surrogateescape").read()

    print(f"[gccrs execute] {case}", file=sys.stderr, flush=True)
    if not target_applies(text):
        print(f"SKIP {case}: target directive does not apply", file=sys.stderr)
        write_stamp(stamp)
        return 0

    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    for key, value in re.findall(
        r'\{\s*dg-set-compiler-env-var\s+([^\s]+)\s+"([^"]*)"', text
    ):
        environment[key] = value

    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        library_path = os.path.join(libstd, "release")
        binary = os.path.join(work, "test")
        compile_result = subprocess.run(
            [
                rustc,
                source,
                "-L", library_path,
                "-o", binary,
                "--crate-type", "bin",
                "--edition", edition(text),
                *compiler_options(text),
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
                cwd=os.path.dirname(source),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=60,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"FAIL {case}: timed out after 60 seconds", file=sys.stderr)
            return 1

        expected_failure = "dg-shouldfail" in text or "dg-xfail-run-if" in text
        if expected_failure:
            if run_result.returncode == 0:
                print(f"FAIL {case}: expected a non-zero exit", file=sys.stderr)
                return 1
        elif run_result.returncode != 0:
            sys.stdout.buffer.write(run_result.stdout)
            print(f"FAIL {case}: exit {run_result.returncode}", file=sys.stderr)
            return run_result.returncode or 1

        expected_output = directives("output", text)
        if expected_output:
            # gccrs directives are Tcl strings containing a regular expression.
            # Tcl consumes one level of doubled backslashes before regexp sees it.
            pattern = "".join(expected_output).replace("\\\\", "\\")
            actual = run_result.stdout.decode("utf-8", errors="surrogateescape")
            if re.search(pattern, actual) is None:
                print(f"FAIL {case}: output does not match dg-output", file=sys.stderr)
                sys.stderr.buffer.write(b"actual:\n" + run_result.stdout)
                return 1

    write_stamp(stamp)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
