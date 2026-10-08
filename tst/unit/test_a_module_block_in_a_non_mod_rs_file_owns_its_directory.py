#!/usr/bin/env python3
"""A module block in a file that is not a `mod.rs` already has that file's
name in its directory: rustc pushes the file's `relative` name and the
block's own name, then lets the block own its directory (`mod_dir_path` in
rustc_expand/src/module.rs gives `DirOwnership::Owned { relative: None }`).
ring's src/polyfill.rs has `pub mod once_cell { pub mod race; }`, found at
src/polyfill/once_cell/race.rs."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_a_module_block_in_a_non_mod_rs_file_owns_its_directory.py RUSTC STAMP")
    rustc, stamp = map(os.path.abspath, sys.argv[1:])

    with tempfile.TemporaryDirectory(prefix="trustme-module-block-directory-") as work:
        root = Path(work)
        files = {
            "lib.rs": "#![feature(no_core)]\n#![no_core]\nmod polyfill;\n",
            "polyfill.rs": "pub mod once_cell {\n    pub mod race;\n}\npub mod a {\n    pub mod b {\n        pub mod c;\n    }\n}\n",
            "polyfill/once_cell/race.rs": "pub struct Race;\n",
            "polyfill/a/b/c.rs": "pub struct C;\n",
        }
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text)
        result = subprocess.run(
            lib.wrap_gdb([
                rustc, "lib.rs", "--crate-type", "lib", "--edition", "2021",
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
            raise RuntimeError("a module block in polyfill.rs did not find its modules under polyfill/")

    Path(stamp).parent.mkdir(parents=True, exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
