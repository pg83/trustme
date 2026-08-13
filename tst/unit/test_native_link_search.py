#!/usr/bin/env python3
"""Check that rustc's `-L native=PATH` reaches the native linker."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: test_native_link_search.py RUSTC SOURCE C_SOURCE "
            "LIBSTD_TAR STAMP"
        )
    rustc, source, c_source, libstd_tar, stamp_text = sys.argv[1:]
    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    environment.setdefault("AR", "ar")

    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(libstd_tar, str(work / "libstd")))
        obj = work / "native_link_search.o"
        archive = work / "libnative_link_search.a"
        binary = work / "test"
        lib.run([environment["CC"], "-c", c_source, "-o", obj], env=environment)
        lib.run([environment["AR"], "rcs", archive, obj], env=environment)
        lib.run(
            lib.wrap_gdb([
                rustc,
                source,
                "-L", str(libstd / "release"),
                "-L", f"native={work}",
                "--cfg", "native_link_search",
                "--edition", "2021",
                "-o", str(binary),
            ]),
            env=environment,
        )
        lib.run([binary], env=environment)

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
