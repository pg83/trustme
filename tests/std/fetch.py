#!/usr/bin/env python3
"""Fetch the rust-1.90.0 standard-library source, patch it, drop in the
mrustc-stdlib shim, and pack the tree into a tar. This is the `std_src` graph
node — shared by every project test.

    fetch.py <out.tar>

Set RUST_SRC to an already-unpacked rust-1.90.0-src tree to skip the download.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import lib  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
VER = "1.90.0"

SHIM_TOML = """\
[package]
name = "mrustc_standard_library"
version = "0.0.0"
[lib]
path = "lib.rs"
[dependencies]
std = { path = "../library/std" }
panic_unwind = { path = "../library/panic_unwind" }
test = { path = "../library/test" }
rustc-std-workspace-core = { path = "../library/rustc-std-workspace-core" }
rustc-std-workspace-alloc = { path = "../library/rustc-std-workspace-alloc" }
rustc-std-workspace-std = { path = "../library/rustc-std-workspace-std" }
"""


def main() -> int:
    out = os.path.abspath(sys.argv[1])
    with lib.workdir() as work:
        src = os.path.join(work, "rust-src")
        local = os.environ.get("RUST_SRC")
        if local:
            shutil.copytree(local, src, symlinks=True)
        else:
            lib.log(f"[std_src] downloading rustc-{VER}-src")
            tarball = os.path.join(work, "src.tar.gz")
            lib.run(["curl", "-sSL", "-o", tarball,
                     f"https://static.rust-lang.org/dist/rustc-{VER}-src.tar.gz"])
            lib.run(["tar", "-C", work, "-xf", tarball])
            os.rename(os.path.join(work, f"rustc-{VER}-src"), src)
            import subprocess
            with open(os.path.join(HERE, f"rustc-{VER}-src.patch")) as patch:
                subprocess.run(["patch", "-p0"], cwd=src, stdin=patch, check=True)
        # The shim pulls std + panic_unwind + test + workspace crates into one build.
        shim = os.path.join(src, "mrustc-stdlib")
        os.makedirs(shim, exist_ok=True)
        with open(os.path.join(shim, "lib.rs"), "w") as fh:
            fh.write("#![no_core]\n")
        with open(os.path.join(shim, "Cargo.toml"), "w") as fh:
            fh.write(SHIM_TOML)

        lib.log(f"[std_src] packing {out}")
        lib.tar_dir(src, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
