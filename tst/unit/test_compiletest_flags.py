#!/usr/bin/env python3
"""Keep vendored rustc test directives byte-compatible with compiletest."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_compiletest_flags.py STAMP")

    actual = lib.compiletest_split_flags(
        '--cfg=feature="rand" '
        '--check-cfg=cfg(feature,values("serde","full")) '
        "--remap-path-prefix '/source path=/mapped path'"
    )
    expected = [
        '--cfg=feature="rand"',
        '--check-cfg=cfg(feature,values("serde","full"))',
        "--remap-path-prefix",
        "/source path=/mapped path",
    ]
    if actual != expected:
        raise RuntimeError(f"compiletest flag split differs: {actual!r} != {expected!r}")

    flags = [
        "-O",
        "-C", "codegen-units=8",
        "-Cno-prepopulate-passes",
        "-Clink-dead-code=on",
        "-Cdebug_assertions=no",
        "-C", "target-feature=-crt-static",
    ]
    expected_mrustc = [
        "-O",
        "-Cdebug_assertions=no",
        "-C", "target-feature=-crt-static",
    ]
    actual_mrustc = lib.mrustc_compile_flags(flags, system_rustc=False)
    if actual_mrustc != expected_mrustc:
        raise RuntimeError(
            f"mrustc backend flag filtering differs: {actual_mrustc!r} != {expected_mrustc!r}"
        )
    if lib.mrustc_compile_flags(flags, system_rustc=True) != flags:
        raise RuntimeError("system rustc flags must remain byte-for-byte intact")

    stamp = os.path.abspath(sys.argv[1])
    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
