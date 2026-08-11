#!/usr/bin/env python3
"""Verify that Windows host and target backends cannot be reintroduced silently."""

import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile


WINDOWS_TARGETS = (
    "i586-pc-windows-gnu",
    "x86_64-pc-windows-gnu",
    "x86-pc-windows-msvc",
    "x86_64-pc-windows-msvc",
)

FORBIDDEN_COMPILER_MARKERS = (
    "_WIN32",
    "_MSC_VER",
    "__MINGW",
    "CodegenMode",
    "Compiler::Msvc",
    "windows.h",
    "pc-windows",
    "mingw32",
    "__declspec",
    "cl.exe",
)


def disable_core_dumps() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_no_windows_support.py RUSTC SOURCE STAMP")

    rustc, source, stamp = map(os.path.abspath, sys.argv[1:])
    for target in WINDOWS_TARGETS:
        result = subprocess.run(
            [rustc, source, "--target", target, "-Zstop-after=parse", "-o", os.devnull],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
            preexec_fn=disable_core_dumps,
        )
        if result.returncode == 0:
            raise RuntimeError(f"removed Windows target {target!r} was accepted")
        if f"Unknown target name '{target}'" not in result.stderr:
            sys.stderr.write(result.stderr)
            raise RuntimeError(f"Windows target {target!r} failed for an unrelated reason")

    with tempfile.TemporaryDirectory() as temp_dir:
        target_spec = Path(temp_dir) / "windows.toml"
        target_spec.write_text(
            """[target]
family = "windows"
os-name = "windows"
env-name = "gnu"
arch = "x86_64"

[backend.c]
variant = "gnu"
target = "x86_64-linux-gnu"
"""
        )
        result = subprocess.run(
            [rustc, source, "--target", str(target_spec), "-Zstop-after=parse", "-o", os.devnull],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            raise RuntimeError("custom Windows target was accepted")
        if "Windows targets are not supported" not in result.stderr:
            sys.stderr.write(result.stderr)
            raise RuntimeError("custom Windows target failed for an unrelated reason")

    rustc_sources = Path(__file__).parents[2] / "bin" / "rustc"
    leftovers = []
    for source_path in sorted(rustc_sources.iterdir()):
        if source_path.suffix not in {".cpp", ".h", ".inc"}:
            continue
        for line_number, line in enumerate(source_path.read_text().splitlines(), 1):
            for marker in FORBIDDEN_COMPILER_MARKERS:
                if marker in line:
                    leftovers.append(
                        f"{source_path.relative_to(rustc_sources.parent.parent)}:{line_number}: {line.strip()}"
                    )
                    break
    if leftovers:
        raise RuntimeError("Windows compiler support remains:\n" + "\n".join(leftovers))

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
