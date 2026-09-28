#!/usr/bin/env python3
"""A proc-macro crate reached through its metadata - `--extern pm=libpm.rlib`,
or a dependency that names `libpm.rlib` found in a `-L` directory - is known
to be a proc macro by that metadata, as rustc knows a proc-macro dylib, and its
plugin stands beside it without `.rlib`. ui_test builds futures' dependents
this way from cargo's `deps` directory."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402

PM = """extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn seven(_input: TokenStream) -> TokenStream {
    "7u32".parse().unwrap()
}
"""

USER = """pub fn seven() -> u32 {
    pm::seven!()
}

#[macro_export]
macro_rules! seven_again {
    () => { $crate::seven() + 0 };
}
"""

MAIN = """fn main() {
    assert_eq!(user::seven(), 7);
    assert_eq!(user::seven_again!(), 7);
}
"""


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_proc_macro_found_by_its_metadata.py RUSTC LIBSTD_TAR STAMP")
    rustc, libstd_tar, stamp_text = sys.argv[1:]
    environment = dict(os.environ)
    environment.setdefault("CC", "cc")

    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(libstd_tar, str(work / "libstd")))
        deps = work / "deps"
        deps.mkdir()
        for name, text in (("pm.rs", PM), ("user.rs", USER), ("main.rs", MAIN)):
            (work / name).write_text(text)
        common = ["--edition", "2021", "-L", str(libstd / "release"), "-L", f"dependency={deps}"]
        lib.run([rustc, str(work / "pm.rs"), "--crate-type", "proc-macro", "--crate-name", "pm",
                 "-o", str(deps / "libpm"), *common], env=environment)
        lib.run([rustc, str(work / "user.rs"), "--crate-type", "lib", "--crate-name", "user",
                 "--extern", f"pm={deps / 'libpm.rlib'}", "-o", str(deps / "libuser.rlib"), *common], env=environment)
        lib.run([rustc, str(work / "main.rs"), "--extern", f"user={deps / 'libuser.rlib'}",
                 "-o", str(work / "main"), *common], env=environment)
        subprocess.run([str(work / "main")], check=True)

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
