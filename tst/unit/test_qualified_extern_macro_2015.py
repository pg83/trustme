#!/usr/bin/env python3
"""Check qualified dependency macro lookup in the Rust 2015 edition."""

import os
import subprocess
import sys
import tempfile


def run(command: list[str], env: dict[str, str]) -> None:
    subprocess.run(command, env=env, timeout=60, check=True)


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: test_qualified_extern_macro_2015.py "
            "RUSTC PRODUCER_RS CONSUMER_RS LIBSTD_TAR STAMP"
        )

    rustc, producer, consumer, libstd_tar, stamp = map(os.path.abspath, sys.argv[1:])
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
    import lib

    with tempfile.TemporaryDirectory(prefix="trustme-qualified-extern-macro-") as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        search = os.path.join(libstd, "release")
        producer_rlib = os.path.join(work, "libqualified_extern_macro_2015_producer.rlib")
        output = os.path.join(work, "consumer")
        env = dict(os.environ)
        env.setdefault("CC", "cc")

        run(
            lib.wrap_gdb([
                rustc,
                producer,
                "--crate-name",
                "qualified_extern_macro_2015_producer",
                "--crate-type",
                "rlib",
                "--edition",
                "2015",
                "-L",
                search,
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
                "2015",
                "-L",
                search,
                "--extern",
                f"qualified_extern_macro_2015_producer={producer_rlib}",
                "-Cemit-cpp-only",
                "-o",
                output,
            ]),
            env,
        )
    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
