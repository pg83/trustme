#!/usr/bin/env python3
"""Run a pinned library project's native Cargo tests with the trustme toolchain.

    test_project.py <src.tar> <vendor.tar.zst> <libstd.tar> <manifest-subdir>
                    [cargo-test-args...]

Environment: RUSTC, CARGO, CC, BUILD_JOBS.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib  # noqa: E402


def main() -> int:
    src_tar = os.path.abspath(sys.argv[1])
    vendor_tar = os.path.abspath(sys.argv[2])
    libstd_tar = os.path.abspath(sys.argv[3])
    subdir = sys.argv[4]
    test_args = sys.argv[5:]
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["TRUSTME_PATH"] = lib.trustme_link(work)
        env.setdefault("CC", "cc")

        src = lib.untar(src_tar, os.path.join(work, "src"))
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        vroot = lib.untar(vendor_tar, os.path.join(work, "vendor"), zstd=True)
        out = os.path.join(work, "out")
        os.makedirs(out, exist_ok=True)

        manifest = os.path.join(src, subdir, "Cargo.toml")
        lib.log(f"[test] {subdir}")
        lib.run([
            cargo, "test", "--locked", "-j", jobs,
            "--manifest-path", manifest,
            "--target-dir", out,
            "-Zvendor-dir=" + os.path.join(vroot, "vendor"),
            "-Zlib-search=" + os.path.join(libstd, "release"),
            *test_args,
        ], cwd=os.path.join(src, subdir), env=env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
