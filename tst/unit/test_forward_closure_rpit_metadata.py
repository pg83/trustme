#!/usr/bin/env python3
"""Check metadata for a closure returning a later closure's RPIT type."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command: list[str], env: dict[str, str]) -> None:
    subprocess.run(command, env=env, timeout=60, check=True)


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: test_forward_closure_rpit_metadata.py "
            "RUSTC PRODUCER_RS CONSUMER_RS LIBSTD_TAR STAMP"
        )

    rustc, producer, consumer, libstd_tar, stamp = map(os.path.abspath, sys.argv[1:])
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    import lib

    with tempfile.TemporaryDirectory(prefix="trustme-forward-closure-rpit-") as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        search = os.path.join(libstd, "release")
        producer_rlib = os.path.join(
            work, "libforward_closure_rpit_metadata_producer.rlib"
        )
        output = os.path.join(work, "consumer")
        env = dict(os.environ)
        env.setdefault("CC", "cc")

        run(
            lib.wrap_gdb([
                rustc,
                producer,
                "--crate-name",
                "forward_closure_rpit_metadata_producer",
                "--crate-type",
                "rlib",
                "--edition",
                "2021",
                "-L",
                search,
                "-Cemit-cpp-only",
                "-o",
                producer_rlib,
            ]),
            env,
        )
        run(
            lib.wrap_gdb([
                rustc,
                consumer,
                "--crate-type",
                "lib",
                "--edition",
                "2021",
                "-L",
                search,
                "--extern",
                "forward_closure_rpit_metadata_producer=" + producer_rlib,
                "-Cemit-cpp-only",
                "-o",
                output,
            ]),
            env,
        )

    Path(stamp).parent.mkdir(parents=True, exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
