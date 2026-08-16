#!/usr/bin/env python3
"""Build and run the attribute proc-macro regression against shared libstd."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    manifest = os.path.abspath(sys.argv[1])
    libstd_tar = os.path.abspath(sys.argv[2])
    stamp = os.path.abspath(sys.argv[3])
    cargo = lib.require_env("CARGO")
    jobs = lib.require_env("BUILD_JOBS")

    with lib.workdir() as work:
        env = dict(os.environ)
        env["TRUSTME_PATH"] = lib.trustme_link(work)
        env["RUSTC_VERSION"] = "1.90.0"
        env["CARGO_TRUSTME_DEFER_CODEGEN"] = "1"
        env.setdefault("CC", "cc")
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        out = os.path.join(work, "out")
        lib.run([
            cargo,
            "build",
            "--release",
            "-j",
            jobs,
            "--manifest-path",
            manifest,
            "--target-dir",
            out,
            "-Zlib-search=" + os.path.join(libstd, "release"),
        ], env=env)
        lib.run([os.path.join(out, "release", "proc_macro_attribute_unit")],
                env=env, timeout=60)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
