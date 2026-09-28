#!/usr/bin/env python3
"""rustc's `--print cfg` lists the configuration a crate is compiled with, one
`name` or `name="value"` per line, leaving out the cfgs a stable compiler
gates (`print_crate_info`). ui_test reads it to pick the target dependencies
of the crates it builds."""

import os
from pathlib import Path
import re
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_print_cfg.py RUSTC STAMP")
    rustc, stamp_text = sys.argv[1:]
    host = next(
        line.split(": ", 1)[1]
        for line in subprocess.run([rustc, "-vV"], capture_output=True, text=True, check=True).stdout.splitlines()
        if line.startswith("host: ")
    )
    for argv in ([rustc, "--print", "cfg", "--target", host], [rustc, "--print=cfg"]):
        lines = subprocess.run(argv, capture_output=True, text=True, check=True).stdout.splitlines()
        for line in lines:
            if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*(="[^"]*")?', line):
                raise SystemExit(f"{argv}: not a cfg line: {line!r}")
        for wanted in ('target_os="linux"', 'target_pointer_width="64"', "unix", "debug_assertions", 'panic="unwind"'):
            if wanted not in lines:
                raise SystemExit(f"{argv}: missing {wanted}: {lines}")
        for gated in ("ub_checks", "overflow_checks", "target_thread_local", 'fmt_debug="full"'):
            if gated in lines:
                raise SystemExit(f"{argv}: prints the gated {gated}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
