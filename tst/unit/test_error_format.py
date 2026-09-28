#!/usr/bin/env python3
"""rustc takes `--error-format` as one of `human`, `short` and `json` on a
stable compiler, reports the unstable `human-unicode`, `human-annotate-rs` and
`pretty-json` as such, and rejects anything else with the list
(`parse_error_format`, rustc_session/src/config.rs). ui_test compiles every
test program with `--error-format=json`."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_error_format.py RUSTC LIBSTD_TAR STAMP")
    rustc, libstd_tar, stamp_text = sys.argv[1:]

    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(libstd_tar, str(work / "libstd")))
        source = work / "program.rs"
        source.write_text("fn main() {}\n")

        def compile_with(fmt: str) -> subprocess.CompletedProcess:
            return subprocess.run(
                [rustc, str(source), "--edition", "2021", "-L", str(libstd / "release"),
                 "-o", str(work / fmt), f"--error-format={fmt}"],
                capture_output=True, text=True,
            )

        for fmt in ("human", "short", "json"):
            run = compile_with(fmt)
            if run.returncode != 0:
                raise SystemExit(f"--error-format={fmt}: exit {run.returncode}\n{run.stderr}")
        for fmt in ("human-unicode", "human-annotate-rs", "pretty-json"):
            run = compile_with(fmt)
            if run.returncode != 1 or f"`--error-format={fmt}` is unstable" not in run.stderr:
                raise SystemExit(f"--error-format={fmt}: exit {run.returncode}\n{run.stderr}")
        run = compile_with("bogus")
        wanted = ("argument for `--error-format` must be `human`, `human-annotate-rs`, `human-unicode`, "
                  "`json`, `pretty-json` or `short` (instead was `bogus`)")
        if run.returncode != 1 or wanted not in run.stderr:
            raise SystemExit(f"--error-format=bogus: exit {run.returncode}\n{run.stderr}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
