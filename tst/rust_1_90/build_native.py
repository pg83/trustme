#!/usr/bin/env python3
"""Build the native helper archive used by Rust's ABI run-pass tests."""

import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_native.py RUST_SRC_TAR OUTPUT")
    source_tar, output_text = sys.argv[1:]
    output = Path(output_text).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work_text:
        work = Path(work_text)
        source = work / "rust_test_helpers.c"
        with tarfile.open(source_tar) as archive:
            member = archive.getmember("./tests/auxiliary/rust_test_helpers.c")
            extracted = archive.extractfile(member)
            if extracted is None:
                raise RuntimeError(f"unable to read {member.name}")
            source.write_bytes(extracted.read())
        obj = work / "rust_test_helpers.o"
        subprocess.run(
            [os.environ.get("CC", "cc"), "-c", source, "-o", obj],
            check=True,
        )
        subprocess.run(
            [os.environ.get("AR", "ar"), "rcs", output, obj],
            check=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
