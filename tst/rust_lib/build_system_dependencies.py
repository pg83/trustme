#!/usr/bin/env python3
"""Build rust-lib dev-dependencies with the external Cargo and rustc."""

import os
from pathlib import Path
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import lib  # noqa: E402


HERE = Path(__file__).resolve().parent


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: build_system_dependencies.py RUST_SRC_TAR OUTPUT_TAR"
        )

    rust_src_tar = os.path.abspath(sys.argv[1])
    output_tar = os.path.abspath(sys.argv[2])
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work_text:
        work = Path(work_text)
        rust_src = Path(lib.untar(rust_src_tar, str(work / "rust-src")))
        output = work / "output"
        vendor = rust_src / "vendor"
        environment = dict(os.environ)
        environment["RUSTC_BOOTSTRAP"] = "1"

        lib.log("[system rust-lib dependencies] rand and rand_xorshift")
        lib.run(
            [
                cargo,
                "build",
                "--offline",
                "--locked",
                "--release",
                "-j",
                jobs,
                "--manifest-path",
                str(HERE / "dependencies" / "Cargo.toml"),
                "--target-dir",
                str(output),
                "--config",
                'source.crates-io.replace-with="vendored-sources"',
                "--config",
                f'source.vendored-sources.directory="{vendor}"',
            ],
            env=environment,
        )
        lib.tar_dir(str(output), output_tar)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
