#!/usr/bin/env python3
"""Check that an interior const pointer survives cross-crate MIR metadata."""

import os
import subprocess
import sys
import tempfile


def run(command: list[str], env: dict[str, str]) -> None:
    subprocess.run(command, env=env, timeout=60, check=True)


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: test_const_borrow_offset_metadata.py "
            "RUSTC PRODUCER_RS CONSUMER_RS LIBSTD_TAR STAMP"
        )

    rustc, producer, consumer, libstd_tar, stamp = map(os.path.abspath, sys.argv[1:])
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    import lib

    with tempfile.TemporaryDirectory(prefix="mrustc-const-offset-metadata-") as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        search = os.path.join(libstd, "release")
        producer_rlib = os.path.join(work, "libconst_borrow_offset_producer.rlib")
        binary = os.path.join(work, "consumer")
        env = dict(os.environ)
        env.setdefault("CC", "cc")

        run(
            [
                rustc,
                producer,
                "--crate-name",
                "const_borrow_offset_producer",
                "--crate-type",
                "rlib",
                "--edition",
                "2021",
                "-L",
                search,
                "-o",
                producer_rlib,
            ],
            env,
        )
        run(
            [
                rustc,
                consumer,
                "--crate-type",
                "bin",
                "--edition",
                "2021",
                "-L",
                search,
                "--extern",
                f"const_borrow_offset_producer={producer_rlib}",
                "-o",
                binary,
            ],
            env,
        )
        run([binary], env)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
