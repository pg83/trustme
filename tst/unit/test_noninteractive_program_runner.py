#!/usr/bin/env python3
"""Test programs must observe EOF even while the gate has a live stdin."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys


def load_module(path: str):
    spec = importlib.util.spec_from_file_location("trustme_program", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load program helper from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_noninteractive_program_runner.py HELPER STAMP")
    helper, stamp = map(os.path.abspath, sys.argv[1:])
    program = load_module(helper)

    read_fd, write_fd = os.pipe()
    try:
        try:
            saved_stdin = os.dup(0)
        except OSError:
            saved_stdin = None
        os.dup2(read_fd, 0)
        result = program.run(
            [
                sys.executable,
                "-c",
                "import sys; raise SystemExit(0 if sys.stdin.buffer.read() == b'' else 1)",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=2,
        )
    finally:
        if saved_stdin is None:
            os.close(0)
        else:
            os.dup2(saved_stdin, 0)
            os.close(saved_stdin)
        os.close(read_fd)
        os.close(write_fd)

    if result.returncode != 0:
        raise RuntimeError(
            f"non-interactive child did not observe EOF: {result.returncode}, "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
