#!/usr/bin/env python3
"""Run a Rust CLI regression with tracing enabled and check stage boundaries."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import selectors
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rustc", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("stamp", type=Path)
    parser.add_argument("--dump", action="store_true")
    args = parser.parse_args()
    rustc, source, archive, stamp = args.rustc, args.source, args.archive, args.stamp
    environment = dict(os.environ, RUSTC=str(rustc.resolve()))
    runner = Path(__file__).with_name("run_one.py")
    markers = (b"ParseModRoot: >>", b"ParseModRoot: <<")
    source_text = source.read_text(encoding="utf-8", errors="surrogateescape")
    if not re.search(r"^//@\s*crate-type:\s*lib\s*$", source_text, re.MULTILINE):
        markers += (b"emitFunctionCode: >>", b"emitFunctionCode: <<")
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        if args.dump:
            wrapper = work / "rustc-dump"
            binary = str(rustc.resolve())
            wrapper.write_text(
                f"#!{sys.executable}\nimport os, sys\n"
                "dumps = [] if os.environ.get('TRUSTME_CLI_EXACT_ARGS') == '1' else "
                "['-Zdump-ast', '-Zdump-hir', '-Zdump-mir']\n"
                f"os.execv({binary!r}, [{binary!r}, *sys.argv[1:], *dumps])\n"
            )
            wrapper.chmod(0o755)
            environment["RUSTC"] = str(wrapper)
        print(f"debug trace: compiling {source.name}", file=sys.stderr, flush=True)
        remaining = set(markers)
        tail = b""
        total = 0
        command = [sys.executable, str(runner), str(source.resolve()),
                   str(archive.resolve()), str(work / "inner.stamp")]
        with subprocess.Popen(command, env=environment, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT) as process:
            deadline = time.monotonic() + 120
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                while True:
                    duration = deadline - time.monotonic()
                    if duration <= 0 or not selector.select(duration):
                        process.kill()
                        raise subprocess.TimeoutExpired(command, 120)
                    block = os.read(process.stdout.fileno(), 1024 * 1024)
                    if not block:
                        break
                    total += len(block)
                    block = tail + block
                    remaining.difference_update(marker for marker in markers if marker in block)
                    tail = block[-16384:]
            result = process.wait(timeout=max(0, deadline - time.monotonic()))
        print(f"debug trace: {total} bytes, exit {result}", file=sys.stderr, flush=True)
        if result:
            sys.stderr.buffer.write(tail)
            raise RuntimeError(f"debug compiler regression failed: {source.name}")
        if remaining:
            raise RuntimeError(f"debug trace is missing stage boundaries: {sorted(remaining)!r}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()


if __name__ == "__main__":
    main()
