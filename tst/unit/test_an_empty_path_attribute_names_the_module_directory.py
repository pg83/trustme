#!/usr/bin/env python3
"""A module block's `#[path]` names its directory, relative to the directory
of the module it is in, and an empty one names that directory itself:
`mod_dir_path` in rustc_expand/src/module.rs joins whatever the attribute
holds (`dir_path.join(path_str)`). opentelemetry-proto's src/proto.rs has
`#[path = "proto/tonic"] mod tonic { #[path = ""] mod collector {
#[path = ""] mod logs { #[path = "...logs.v1.rs"] mod v1; } } }`, all in
src/proto/tonic."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_an_empty_path_attribute_names_the_module_directory.py RUSTC STAMP")
    rustc, stamp = map(os.path.abspath, sys.argv[1:])

    with tempfile.TemporaryDirectory(prefix="trustme-empty-path-attribute-") as work:
        root = Path(work)
        files = {
            "lib.rs": "#![feature(no_core)]\n#![no_core]\nmod proto;\n",
            "proto.rs": (
                '#[path = "proto/tonic"]\n'
                "pub mod tonic {\n"
                '    #[path = ""]\n'
                "    pub mod collector {\n"
                '        #[path = ""]\n'
                "        pub mod logs {\n"
                '            #[path = "collector.logs.v1.rs"]\n'
                "            pub mod v1;\n"
                "        }\n"
                "    }\n"
                "}\n"
            ),
            "proto/tonic/collector.logs.v1.rs": "pub struct V1;\n",
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
            raise RuntimeError("an empty #[path] did not keep the module block in its directory")

    Path(stamp).parent.mkdir(parents=True, exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
