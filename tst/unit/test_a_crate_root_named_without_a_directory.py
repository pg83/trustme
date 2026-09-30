#!/usr/bin/env python3
"""A crate root named with no directory, as cargo names the build script of
the package it runs in (`rustc build.rs`), has the empty directory: rustc joins
a `#[path]` or an out-of-line module onto it (`Path::join` of `""`), which is
the path as written, relative to the working directory. mime_guess's build.rs
has `#[path = "src/mime_types.rs"] mod mime_types;`."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_a_crate_root_named_without_a_directory.py RUSTC STAMP")
    rustc, stamp = map(os.path.abspath, sys.argv[1:])

    with tempfile.TemporaryDirectory(prefix="trustme-root-without-directory-") as work:
        root = Path(work)
        (root / "src").mkdir()
        (root / "build.rs").write_text(
            "#![feature(no_core)]\n"
            "#![no_core]\n"
            '#[path = "src/types.rs"]\n'
            "mod types;\n"
            "mod other;\n"
        )
        (root / "src" / "types.rs").write_text("pub struct Types;\n")
        (root / "other.rs").write_text("pub struct Other;\n")
        result = subprocess.run(
            lib.wrap_gdb([
                rustc, "build.rs", "--crate-type", "lib", "--edition", "2021",
                "-Zstop-after=expand", "-o", str(root / "out"),
            ]),
            cwd=work,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise RuntimeError("a crate root named without a directory did not load its modules")

    Path(stamp).parent.mkdir(parents=True, exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
