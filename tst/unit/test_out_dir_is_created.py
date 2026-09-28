#!/usr/bin/env python3
"""rustc creates the `--out-dir` it is given, parents included, before it
writes there, and fails when it cannot (`output_filenames`,
rustc_interface/src/passes.rs); a `--print file-names` run stops before that
and creates nothing. ui_test hands every test program the `target/ui_test`
directory it never makes itself."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_out_dir_is_created.py RUSTC LIBSTD_TAR STAMP")
    rustc, libstd_tar, stamp_text = sys.argv[1:]

    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(libstd_tar, str(work / "libstd")))
        source = work / "program.rs"
        source.write_text("fn main() {}\n")
        common = ["--edition", "2021", "-L", str(libstd / "release")]

        printed = work / "printed" / "dir"
        subprocess.run([rustc, str(source), "--out-dir", str(printed), *common, "--print", "file-names"],
                       capture_output=True, check=True)
        if (work / "printed").exists():
            raise SystemExit("--print file-names created the out dir")

        nested = work / "target" / "ui_test"
        subprocess.run([rustc, str(source), "--out-dir", str(nested), *common], check=True)
        if not (nested / "program").is_file():
            raise SystemExit(f"no program in {nested}: {sorted(path.name for path in work.rglob('*'))}")

        blocker = work / "file"
        blocker.write_text("")
        run = subprocess.run([rustc, str(source), "--out-dir", str(blocker / "dir"), *common],
                             capture_output=True, text=True)
        if run.returncode != 1 or "failed to find or create the directory specified by `--out-dir`" not in run.stderr:
            raise SystemExit(f"--out-dir under a file: exit {run.returncode}\n{run.stderr}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
