#!/usr/bin/env python3
"""Attributes belong to more positions than free items.

`#[target_feature]` on an inherent or provided method, `#[inline]` on a closure
and a `///` on a `macro_rules!` inside a function body are all accepted in
silence by rustc 1.90 - see `check_target_feature`/`check_inline`
(rustc_passes/src/check_attr.rs) and `UnusedDocComment::check_stmt`
(rustc_lint/src/builtin.rs). The positions upstream rejects must still be
rejected here.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


REJECTED = (
    (
        "target_feature on a required trait method",
        """#![feature(no_core)]
#![no_core]
pub trait T {
    #[target_feature(enable = "avx2")]
    unsafe fn required(&self);
}
""",
        "#[target_feature] should be applied to a function definition",
    ),
    (
        "target_feature on an associated constant",
        """#![feature(no_core)]
#![no_core]
pub struct S;
impl S {
    #[target_feature(enable = "avx2")]
    pub const UNUSED: u32 = 1;
}
""",
        "#[target_feature] should be applied to a function definition",
    ),
    (
        "inline on a plain expression",
        """#![feature(no_core)]
#![no_core]
pub fn takes<F>(_f: F) {}
pub fn f() {
    takes(#[inline] (1u32, 2u32));
}
""",
        "#[inline] should be applied to a function or closure",
    ),
)


def compile_lib(rustc: str, src: str, out: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        lib.wrap_gdb([
            rustc, src, "--crate-type", "lib", "--edition", "2021",
            "-Zstop-after=expand", "-o", out,
        ]),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_attribute_targets.py RUSTC INPUT_RS STAMP")

    rustc, src, stamp = map(os.path.abspath, sys.argv[1:])

    with tempfile.TemporaryDirectory(prefix="trustme-attribute-targets-") as work:
        accepted = compile_lib(rustc, src, os.path.join(work, "accepted"))
        if accepted.returncode != 0:
            sys.stdout.write(accepted.stdout)
            sys.stderr.write(accepted.stderr)
            raise RuntimeError("accepted attribute positions did not compile")
        if "Unexpected attribute" in accepted.stderr:
            sys.stderr.write(accepted.stderr)
            raise RuntimeError("a position rustc accepts drew an 'Unexpected attribute' warning")

        for index, (what, source, needle) in enumerate(REJECTED):
            rejected_src = Path(work) / f"rejected{index}.rs"
            rejected_src.write_text(source)
            result = compile_lib(rustc, str(rejected_src), os.path.join(work, f"rejected{index}"))
            if result.returncode == 0:
                raise RuntimeError(f"{what} was accepted")
            if needle not in result.stderr:
                sys.stderr.write(result.stderr)
                raise RuntimeError(f"{what} did not report {needle!r}")

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
