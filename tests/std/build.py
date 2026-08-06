#!/usr/bin/env python3
"""Build the mrustc standard library (libcore/liballoc/libstd/libtest/…) plus
libproc_macro from a packed rust-src tree, into a tar. This is the `libstd`
graph node — built once, depended on by every project build.

    build.py <rust-src.tar> <out.tar>

Environment: RUSTC, MINICARGO, CC.
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
    minicargo = lib.require_env("MINICARGO")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["MRUSTC_PATH"] = lib.mrustc_link(work)
        env["MRUSTC_TARGET_VER"] = "1.90"
        env["RUSTC_VERSION"] = "1.90.0"
        env["STD_ENV_ARCH"] = env.get("STD_ENV_ARCH", "x86_64")
        env["MINICARGO_DEFER_CODEGEN"] = "0"
        env.setdefault("CC", "cc")

        src = lib.untar(src_tar, os.path.join(work, "rust-src"))
        outdir = os.path.join(work, "libstd")
        os.makedirs(outdir, exist_ok=True)

        lib.log("[libstd] standard library")
        lib.run([minicargo,
                 "--vendor-dir", os.path.join(src, "vendor"),
                 "--script-overrides", OVERRIDES,
                 "--manifest-overrides", MANIFEST_OVERRIDES,
                 "--output-dir", outdir,
                 os.path.join(src, "mrustc-stdlib") + os.sep],
                env=env)

        lib.log("[libstd] libproc_macro")
        lib.run([minicargo, "--output-dir", outdir,
                 os.path.join(HERE, "libproc_macro")],
                env=env)

        lib.log(f"[libstd] packing {out}")
        lib.tar_dir(outdir, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
