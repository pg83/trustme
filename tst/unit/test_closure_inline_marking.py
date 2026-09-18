#!/usr/bin/env python3
"""`#[inline]` on a closure reaches the code generated for that closure.

Upstream `check_inline` (rustc_passes/src/check_attr.rs) accepts
`Target::Closure` beside `Target::Fn`, and `codegen_fn_attrs`
(rustc_codegen_ssa/src/codegen_attrs.rs) stores the parsed `InlineAttr` on the
closure's own def. Here the closure's body becomes a `call_once` method, so the
marking has to arrive there and turn into `__attribute__((always_inline))` - the
same thing `#[inline(always)]` on a free function does.
"""

import os
from pathlib import Path
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


ALWAYS_INLINE = re.compile(r"__attribute__\(\(always_inline\)\)[^(]*?(ZR[0-9a-f]{16})\(")


def emit_cpp(rustc: str, src: str, search: str, out: str, env: dict[str, str], marked: bool) -> str:
    lib.run(
        lib.wrap_gdb([
            rustc, src, "-L", search, "--crate-type", "bin", "--edition", "2021",
            *(["--cfg", "closure_inline_marking"] if marked else []),
            "-Cemit-cpp-only", "-o", out,
        ]),
        env=env,
        timeout=600,
    )
    return Path(out + ".cpp").read_text()


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: test_closure_inline_marking.py RUSTC INPUT_RS LIBSTD_TAR STAMP"
        )

    rustc, src, libstd_tar, stamp = map(os.path.abspath, sys.argv[1:])
    with lib.workdir() as work:
        env = dict(os.environ)
        env.setdefault("CC", "cc")
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        search = os.path.join(libstd, "release")

        plain = emit_cpp(rustc, src, search, os.path.join(work, "plain"), env, marked=False)
        marked = emit_cpp(rustc, src, search, os.path.join(work, "marked"), env, marked=True)

    gained = set(ALWAYS_INLINE.findall(marked)) - set(ALWAYS_INLINE.findall(plain))
    if not gained:
        raise RuntimeError(
            "generated C++ lost #[inline(always)] on a closure: the attribute "
            "marked no function the unmarked build left alone"
        )

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
