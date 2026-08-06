#!/usr/bin/env python3
"""Build one project offline against a prebuilt libstd and run its test. This is
a `<proj>` graph node (the plan's node 2: "builds and runs the tests"). It
depends on the shared libstd tar, never rebuilding it.

    build_project.py <src.tar> <vendor.tar.zst> <libstd.tar> <manifest-subdir> <test-cmd...>

The test command runs with @BIN@ replaced by the freshly built executable.
Environment: RUSTC, MINICARGO, CC, BUILD_JOBS.
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
    test_cmd = sys.argv[5:]
    minicargo = lib.require_env("MINICARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["MRUSTC_PATH"] = lib.mrustc_link(work)
        env["MRUSTC_TARGET_VER"] = "1.90"
        env["MINICARGO_DEFER_CODEGEN"] = "0"
        env.setdefault("CC", "cc")

        src = lib.untar(src_tar, os.path.join(work, "src"))
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        vroot = lib.untar(vendor_tar, os.path.join(work, "vendor"), zstd=True)
        out = os.path.join(work, "out")
        os.makedirs(out, exist_ok=True)

        lib.log(f"[build] {subdir}")
        lib.run([minicargo, "-j", jobs, ".",
                 "--vendor-dir", os.path.join(vroot, "vendor"),
                 "-L", libstd,
                 "--output-dir", out],
                cwd=os.path.join(src, subdir), env=env)

        binary = os.path.join(out, os.path.basename(subdir))
        if not os.access(binary, os.X_OK):
            binary = next(
                (os.path.join(out, f) for f in sorted(os.listdir(out))
                 if os.access(os.path.join(out, f), os.X_OK)
                 and os.path.isfile(os.path.join(out, f))),
                binary)

        lib.log(f"[test] {binary}")
        lib.run([binary if a == "@BIN@" else a for a in test_cmd])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
