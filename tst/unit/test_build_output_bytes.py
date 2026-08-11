#!/usr/bin/env python3
"""The graph runner must preserve arbitrary command stderr byte-for-byte."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


BUILD_PY = r'''
import build

build.flags.allow({})

success = command(
    name="success",
    inputs=[],
    outputs=["$(B)/success.stamp"],
    cmd=[
        "python3",
        "-c",
        "import pathlib,sys; sys.stderr.buffer.write(b'success-\\xff\\n'); pathlib.Path(sys.argv[1]).touch()",
        "$(B)/success.stamp",
    ],
)

failure = command(
    name="failure",
    inputs=[],
    outputs=["$(B)/failure.stamp"],
    cmd=[
        "python3",
        "-c",
        "import sys; sys.stderr.buffer.write(b'failure-\\xfe\\n'); raise SystemExit(7)",
    ],
)
'''


def run(build: str, root: str, target: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        [sys.executable, build, "--build-file", os.path.join(root, "build.py"),
         "-B", os.path.join(root, "out"), target],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_build_output_bytes.py BUILD STAMP")
    source_build, stamp = map(os.path.abspath, sys.argv[1:])

    with tempfile.TemporaryDirectory(prefix="trustme-build-output-bytes-") as root:
        build = os.path.join(root, "build")
        shutil.copy2(source_build, build)
        Path(root, "build.py").write_text(BUILD_PY, encoding="utf-8")

        success = run(build, root, "success")
        if success.returncode != 0:
            sys.stderr.buffer.write(success.stderr)
            raise RuntimeError(f"successful byte-output node exited {success.returncode}")
        if b"success-\xff\n" not in success.stderr:
            raise RuntimeError(f"successful stderr bytes changed: {success.stderr!r}")

        failure = run(build, root, "failure")
        if failure.returncode != 1:
            sys.stderr.buffer.write(failure.stderr)
            raise RuntimeError(f"failed byte-output node exited {failure.returncode}")
        if b"failure-\xfe\n" not in failure.stderr:
            raise RuntimeError(f"failed stderr bytes changed: {failure.stderr!r}")
        if b"command exited 7" not in failure.stderr:
            raise RuntimeError(f"failed command diagnostic is missing: {failure.stderr!r}")
        if b"codec can't decode" in failure.stderr:
            raise RuntimeError(f"runner attempted strict decoding: {failure.stderr!r}")

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
