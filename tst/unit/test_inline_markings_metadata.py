#!/usr/bin/env python3
"""Check that Rust inline markings survive rlib metadata and reach C++ codegen."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def run(command: list[str], env: dict[str, str]) -> None:
    subprocess.run(command, env=env, timeout=60, check=True)


def mangled_symbol(generated: str, function: str) -> str:
    match = re.search(
        rf"^#define (ZR[0-9a-f]{{16}}) {re.escape(function)}$",
        generated,
        re.MULTILINE,
    )
    if match is None:
        raise RuntimeError(f"generated C++ lost the exported function {function}")
    return match.group(1)


def require_always_inline(generated: str, function: str) -> None:
    symbol = mangled_symbol(generated, function)
    declarations = [
        line for line in generated.splitlines()
        if symbol in line and "(" in line
    ]
    if not any("__attribute__((always_inline))" in line for line in declarations):
        raise RuntimeError(f"generated C++ lost the inline marking for {function}")


def require_not_always_inline(generated: str, function: str) -> None:
    symbol = mangled_symbol(generated, function)
    declarations = [
        line for line in generated.splitlines()
        if symbol in line and "(" in line
    ]
    if not declarations:
        raise RuntimeError(f"generated C++ lost the function {function}")
    if any("__attribute__((always_inline))" in line for line in declarations):
        raise RuntimeError(f"generated C++ forced ordinary #[inline] for {function}")


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: test_inline_markings_metadata.py "
            "RUSTC PRODUCER_RS CONSUMER_RS LIBSTD_TAR STAMP"
        )

    rustc, producer, consumer, libstd_tar, stamp = map(os.path.abspath, sys.argv[1:])
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    import lib

    with tempfile.TemporaryDirectory(prefix="trustme-inline-markings-") as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        search = os.path.join(libstd, "release")
        producer_rlib = os.path.join(work, "libinline_markings_producer.rlib")
        output = os.path.join(work, "consumer")
        env = dict(os.environ)
        env.setdefault("CC", "cc")

        run(
            lib.wrap_gdb([
                rustc,
                producer,
                "--crate-name",
                "inline_markings_producer",
                "--crate-type",
                "rlib",
                "--edition",
                "2021",
                "-L",
                search,
                "-o",
                producer_rlib,
            ]),
            env,
        )
        run(
            lib.wrap_gdb([
                rustc,
                consumer,
                "--crate-type",
                "bin",
                "--edition",
                "2021",
                "--cfg",
                "inline_markings_metadata",
                "-L",
                search,
                "--extern",
                f"inline_markings_producer={producer_rlib}",
                "-Cemit-cpp-only",
                "-o",
                output,
            ]),
            env,
        )

        consumer_generated = Path(output + ".cpp").read_text()
        require_not_always_inline(consumer_generated, "trustme_inline_normal_probe")
        require_always_inline(consumer_generated, "trustme_inline_always_probe")

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
