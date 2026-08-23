#!/usr/bin/env python3
"""Build Rust 1.90 library-test dev-dependencies once for all harnesses."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import lib  # noqa: E402


HERE = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: build_dependencies.py RUST_SRC_TAR LIBSTD_TAR OUTPUT_TAR"
        )

    rust_src_tar = os.path.abspath(sys.argv[1])
    libstd_tar = os.path.abspath(sys.argv[2])
    output_tar = os.path.abspath(sys.argv[3])
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        environment = dict(os.environ)
        environment["TRUSTME_PATH"] = lib.trustme_link(work)
        environment.setdefault("CC", "cc")

        rust_src = lib.untar(rust_src_tar, os.path.join(work, "rust-src"))
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        output = os.path.join(work, "output")
        os.makedirs(output, exist_ok=True)

        lib.log("[rust-lib dependencies] rand and rand_xorshift")
        lib.run(
            [
                cargo,
                "build",
                "--release",
                "-j",
                jobs,
                "--manifest-path",
                os.path.join(HERE, "dependencies", "Cargo.toml"),
                "--target-dir",
                output,
                "-Zpublish-deps",
                "-Zvendor-dir=" + os.path.join(rust_src, "vendor"),
                "-Zlib-search=" + os.path.join(libstd, "release"),
            ],
            env=environment,
        )
        lib.tar_dir(output, output_tar)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
