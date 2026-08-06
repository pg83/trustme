#!/usr/bin/env python3
"""Build the mrustc standard library (libcore/liballoc/libstd/libtest/…) plus
libproc_macro from a packed rust-src tree, into a tar. This is the `libstd`
graph node — built once, depended on by every project build.

    build.py <rust-src.tar> <out.tar>

Environment: RUSTC, CARGO, CC, BUILD_JOBS.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import lib  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OVERRIDES = os.path.join(HERE, "script-overrides", "stable-1.90.0-linux")
MANIFEST_OVERRIDES = os.path.join(HERE, "rustc-1.90.0-overrides.toml")


def main() -> int:
    src_tar = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2])
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["MRUSTC_PATH"] = lib.mrustc_link(work)
        env["MRUSTC_TARGET_VER"] = "1.90"
        env["RUSTC_VERSION"] = "1.90.0"
        env["STD_ENV_ARCH"] = env.get("STD_ENV_ARCH", "x86_64")
        env["CARGO_MRUSTC_DEFER_CODEGEN"] = "1"
        env.setdefault("CC", "cc")

        src = lib.untar(src_tar, os.path.join(work, "rust-src"))
        outdir = os.path.join(work, "libstd")
        os.makedirs(outdir, exist_ok=True)

        lib.log("[libstd] standard library")
        lib.run([cargo, "build", "--release", "-j", jobs,
                 "--manifest-path", os.path.join(src, "mrustc-stdlib", "Cargo.toml"),
                 "--target-dir", outdir,
                 "-Zvendor-dir=" + os.path.join(src, "vendor"),
                 "-Zscript-overrides=" + OVERRIDES,
                 "-Zmanifest-overrides=" + MANIFEST_OVERRIDES],
                env=env)

        lib.log("[libstd] libproc_macro")
        lib.run([cargo, "build", "--release", "-j", jobs,
                 "--manifest-path", os.path.join(HERE, "libproc_macro", "Cargo.toml"),
                 "--target-dir", outdir],
                env=env)

        lib.log(f"[libstd] packing {out}")
        lib.tar_dir(outdir, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
