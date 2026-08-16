#!/usr/bin/env python3
"""Build one project offline against a prebuilt libstd and run its test. This is
a `<proj>` graph node (the plan's node 2: "builds and runs the tests"). It
depends on the shared libstd tar, never rebuilding it.

    build_project.py <src.tar> <vendor.tar.zst> <libstd.tar> <manifest-subdir> <test-cmd...>

The test command runs with @BIN@ replaced by the freshly built executable.
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
    test_cmd = sys.argv[5:]
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["TRUSTME_PATH"] = lib.trustme_link(work)
        env["CARGO_TRUSTME_DEFER_CODEGEN"] = "1"
        env.setdefault("CC", "cc")

        src = lib.untar(src_tar, os.path.join(work, "src"))
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        vroot = lib.untar(vendor_tar, os.path.join(work, "vendor"), zstd=True)
        out = os.path.join(work, "out")
        os.makedirs(out, exist_ok=True)

        lib.log(f"[build] {subdir}")
        lib.run([cargo, "build", "--release", "-j", jobs,
                 "--manifest-path", os.path.join(src, subdir, "Cargo.toml"),
                 "--target-dir", out,
                 "-Zvendor-dir=" + os.path.join(vroot, "vendor"),
                 "-Zlib-search=" + os.path.join(libstd, "release")],
                cwd=os.path.join(src, subdir), env=env)

        binary = os.path.join(out, "release", os.path.basename(subdir))
        if not os.access(binary, os.X_OK):
            binary = next(
                (os.path.join(root, f)
                 for root, _, files in os.walk(out)
                 for f in sorted(files)
                 if os.access(os.path.join(root, f), os.X_OK)
                 and os.path.isfile(os.path.join(root, f))),
                binary)

        lib.log(f"[test] {binary}")
        lib.run([binary if a == "@BIN@" else a for a in test_cmd], timeout=60)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
