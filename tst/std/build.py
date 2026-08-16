#!/usr/bin/env python3
"""Build the trustme standard library (libcore/liballoc/libstd/libtest/…) plus
libproc_macro from a packed rust-src tree, into a tar. This is the `libstd`
graph node — built once, depended on by every project build.

    build.py <rust-src.tar> <out.tar> <proc-macro-manifest>

Environment: RUSTC, CARGO, CC, BUILD_JOBS.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import lib  # noqa: E402

def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: build.py RUST_SRC_TAR OUT_TAR PROC_MACRO_MANIFEST"
        )
    src_tar = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2])
    proc_macro_manifest = os.path.abspath(sys.argv[3])
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["TRUSTME_PATH"] = lib.trustme_link(work)
        env["RUSTC_VERSION"] = "1.90.0"
        env["STD_ENV_ARCH"] = env.get("STD_ENV_ARCH", "x86_64")
        env["CARGO_TRUSTME_DEFER_CODEGEN"] = "1"
        env.setdefault("CC", "cc")

        src = lib.untar(src_tar, os.path.join(work, "rust-src"))
        outdir = os.path.join(work, "libstd")
        os.makedirs(outdir, exist_ok=True)

        lib.log("[libstd] standard library")
        lib.run([cargo, "build", "--release", "-j", jobs,
                 "--manifest-path", os.path.join(src, "trustme-stdlib", "Cargo.toml"),
                 "--target-dir", outdir,
                 "-Zvendor-dir=" + os.path.join(src, "vendor")],
                env=env)

        lib.log("[libstd] libproc_macro")
        lib.run([cargo, "build", "--release", "--lib", "-j", jobs,
                 "--manifest-path", proc_macro_manifest,
                 "--target-dir", outdir],
                env=env)

        lib.log(f"[libstd] packing {out}")
        lib.tar_dir(outdir, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
