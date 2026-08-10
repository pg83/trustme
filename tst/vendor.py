#!/usr/bin/env python3
"""Vendor a project's locked dependencies into a hermetic tar.zst with the Go
cargo. This is a `<proj>_vendor` graph node.

    vendor.py <project-src.tar> <manifest-subdir> <out.tar.zst>

Environment: CARGO. Set SSL_CERT_FILE if the environment lacks system certs.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib  # noqa: E402


def main() -> int:
    src_tar = os.path.abspath(sys.argv[1])
    subdir = sys.argv[2]  # dir within the source tree holding Cargo.lock (may be ".")
    out = os.path.abspath(sys.argv[3])
    cargo = lib.require_env("CARGO")

    with lib.workdir() as work:
        src = lib.untar(src_tar, os.path.join(work, "src"))
        lib.run([cargo, "vendor",
                 "--manifest-path", os.path.join(src, subdir, "Cargo.toml"),
                 "-Zarchive=" + out])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
