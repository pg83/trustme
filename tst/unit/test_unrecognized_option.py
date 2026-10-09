#!/usr/bin/env python3
"""rustc parses its command line with getopts and reports a long option it
does not know as `Unrecognized option: 'NAME'`, the name without its dashes
or any `=value`, through `early_fatal`: `error: ` before it, a blank line
after it, exit status 1. rustc_version's `rustc_error` test runs
`rustc --FOO` and compares the whole of stderr."""

from pathlib import Path
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_unrecognized_option.py RUSTC STAMP")
    rustc, stamp_text = sys.argv[1:]
    for argument in ("--FOO", "--FOO=1"):
        run = subprocess.run([rustc, argument], capture_output=True, text=True)
        wanted = "error: Unrecognized option: 'FOO'\n\n"
        if run.returncode != 1 or run.stderr != wanted:
            raise SystemExit(f"{argument}: exit {run.returncode}, stderr {run.stderr!r}, wanted {wanted!r}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
