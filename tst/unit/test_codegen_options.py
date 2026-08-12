#!/usr/bin/env python3
"""Check that rustc-compatible -C options drive cfg, MIR, and C codegen."""

import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def invoke(rustc: str, src: str, output: str, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        lib.wrap_gdb([rustc, src, "--crate-type", "lib", "-o", output, *args]),
        env=dict(os.environ),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def expect_ok(result: subprocess.CompletedProcess[str], what: str) -> None:
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"{what} failed with exit {result.returncode}")


def expect_error(result: subprocess.CompletedProcess[str], what: str, needle: str) -> None:
    if result.returncode == 0:
        raise RuntimeError(f"{what} was accepted")
    if needle not in result.stderr:
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"{what} did not report {needle!r}")


def compile_mir(rustc: str, src: str, work: str, name: str, args: list[str]) -> str:
    output = os.path.join(work, name)
    result = invoke(
        rustc,
        src,
        output,
        ["--crate-name", "codegen_options", "-Zdump-mir", "-Zstop-after=mir", *args],
    )
    expect_ok(result, name)
    return Path(output + "_3_mir.rs").read_text()


def ordinary_call_is_inlined(mir: str) -> bool:
    marker = 'fn ::"codegen_options"::ordinary_call('
    start = mir.find(marker)
    if start < 0:
        raise RuntimeError("MIR dump has no ordinary_call")
    end = mir.find("\nfn ", start + len(marker))
    body = mir[start:] if end < 0 else mir[start:end]
    return '::"codegen_options"::ordinary(' not in body


def check_cfg(rustc: str, src: str, work: str, name: str, expected: bool, args: list[str]) -> None:
    expectation = "expect_debug_assertions" if expected else "expect_no_debug_assertions"
    result = invoke(
        rustc,
        src,
        os.path.join(work, name),
        ["-Zstop-after=expand", f"--cfg={expectation}", *args],
    )
    expect_ok(result, name)


def codegen_flag(rustc: str, src: str, work: str, name: str, level: str) -> list[str]:
    output = os.path.join(work, name)
    command_file = output + ".command"
    result = invoke(
        rustc,
        src,
        output,
        [f"-Cemit-build-command={command_file}", f"-Copt-level={level}"],
    )
    expect_ok(result, f"C codegen opt-level={level}")
    command = shlex.split(Path(command_file).read_text())
    response = next((arg[1:] for arg in command if arg.startswith("@")), None)
    if response is None:
        raise RuntimeError(f"C codegen command has no response file: {command!r}")
    return shlex.split(Path(response).read_text())


def check_link_args(rustc: str, src: str, work: str) -> None:
    output = os.path.join(work, "link-args")
    command_file = output + ".command"
    expected = ["trustme-link-arg-first", "trustme-link-arg-second"]
    result = subprocess.run(
        lib.wrap_gdb([
            rustc,
            src,
            "--crate-type",
            "bin",
            "-o",
            output,
            f"-Cemit-build-command={command_file}",
            f"-Clink-arg={expected[0]}",
            "-C",
            f"link-arg={expected[1]}",
        ]),
        env=dict(os.environ),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    expect_ok(result, "link-arg codegen")
    command = shlex.split(Path(command_file).read_text())
    response = next((arg[1:] for arg in command if arg.startswith("@")), None)
    if response is None:
        raise RuntimeError(f"link-arg command has no response file: {command!r}")
    args = shlex.split(Path(response).read_text())
    actual = [arg for arg in args if arg.startswith("trustme-link-arg-")]
    if actual != expected:
        raise RuntimeError(f"link args changed order: {actual!r} != {expected!r}")


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: test_codegen_options.py RUSTC MIR_RS CFG_RS LINK_RS STAMP"
        )
    rustc, mir_src, cfg_src, link_src, stamp = map(os.path.abspath, sys.argv[1:])

    with tempfile.TemporaryDirectory(prefix="mrustc-codegen-options-") as work:
        mir_cases = [
            ("level0", ["-Copt-level=0"], False),
            ("level1", ["-C", "opt-level=1"], False),
            ("level2", ["-Copt-level=2"], True),
            ("level3", ["-C", "opt-level=3"], True),
            ("size", ["-Copt-level=s"], True),
            ("size_min", ["-Copt-level=z"], True),
            ("rightmost_c", ["-O", "-Copt-level=0"], False),
            ("rightmost_o", ["-Copt-level=0", "-O"], True),
        ]
        for name, args, expected_inline in mir_cases:
            actual_inline = ordinary_call_is_inlined(compile_mir(rustc, mir_src, work, name, args))
            if actual_inline != expected_inline:
                raise RuntimeError(f"{name}: ordinary_call inline={actual_inline}, expected {expected_inline}")

        check_cfg(rustc, cfg_src, work, "default", True, [])
        check_cfg(rustc, cfg_src, work, "optimized", False, ["-O"])
        check_cfg(rustc, cfg_src, work, "level0-cfg", True, ["-Copt-level=0"])
        check_cfg(rustc, cfg_src, work, "size-cfg", False, ["-Copt-level=s"])
        check_cfg(rustc, cfg_src, work, "bare-debug", True, ["-O", "-C", "debug-assertions"])
        check_cfg(rustc, cfg_src, work, "yes-debug", True, ["-O", "-Cdebug-assertions=yes"])
        check_cfg(rustc, cfg_src, work, "no-debug", False, ["-Cdebug-assertions=no"])
        check_cfg(rustc, cfg_src, work, "off-debug", False, ["-C", "debug-assertions=off"])

        expect_error(
            invoke(rustc, cfg_src, os.path.join(work, "invalid-level"), ["-Copt-level=4"]),
            "invalid opt-level",
            "optimization level",
        )
        expect_error(
            invoke(rustc, cfg_src, os.path.join(work, "invalid-debug"), ["-Cdebug-assertions=maybe"]),
            "invalid debug-assertions",
            "debug-assertions",
        )

        expected_codegen_flags = {"0": "-O0", "1": "-O1", "s": "-Os", "z": "-Oz"}
        for level, expected_flag in expected_codegen_flags.items():
            flags = codegen_flag(rustc, mir_src, work, f"codegen-{level}", level)
            if expected_flag not in flags:
                raise RuntimeError(f"opt-level={level} did not emit {expected_flag}: {flags!r}")
        check_link_args(rustc, link_src, work)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
