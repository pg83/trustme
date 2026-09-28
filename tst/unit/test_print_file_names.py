#!/usr/bin/env python3
"""rustc's `--print file-names` names the file a compilation would write, and
writes nothing. ui_test compiles each test program and then asks this, to find
the executable it runs."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_print_file_names.py RUSTC LIBSTD_TAR STAMP")
    rustc, libstd_tar, stamp_text = sys.argv[1:]

    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(libstd_tar, str(work / "libstd")))
        source = work / "basic_example.rs"
        source.write_text("fn main() {}\n")
        out = work / "out"
        out.mkdir()
        cases = [
            ([str(source), "--out-dir", str(out)], "basic_example"),
            (["--crate-type=lib", "--crate-name", "probe", "--out-dir", str(out), "-"], "libprobe.rlib"),
            ([str(source), "-o", str(work / "renamed")], "renamed"),
        ]
        for args, wanted in cases:
            printed = subprocess.run(
                [rustc, *args, "--edition", "2021", "-L", str(libstd / "release"), "--print", "file-names"],
                input="pub fn probe() {}\n", capture_output=True, text=True, check=True,
            ).stdout.split()
            if printed != [wanted]:
                raise SystemExit(f"{args}: printed {printed}, want {[wanted]}")
        written = sorted(path.name for path in out.iterdir())
        if written:
            raise SystemExit(f"--print file-names wrote {written}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
