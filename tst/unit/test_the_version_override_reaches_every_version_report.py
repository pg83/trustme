#!/usr/bin/env python3
"""Upstream rustc reports `RUSTC_OVERRIDE_VERSION_STRING`, when set, as its
version everywhere it reports one: the first line and the `release:` field of
`-vV` (`version_at_macro_invocation`, rustc_driver_impl) and the version
`cfg(version(..))` compares with (`RustcVersion::current_overridable`). Cargo
sets it to the version the project asks for; build scripts read `-vV`. The
compiler has no version of its own: with nothing given, `-vV` fails."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def run(cmd, env, cwd=None):
    return subprocess.run(
        lib.wrap_gdb(cmd), cwd=cwd, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
    )


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_the_version_override_reaches_every_version_report.py RUSTC STAMP")
    rustc, stamp = map(os.path.abspath, sys.argv[1:])

    bare = dict(os.environ)
    bare.pop("RUSTC_OVERRIDE_VERSION_STRING", None)
    result = run([rustc, "-vV"], bare)
    if result.returncode == 0 or result.stdout.startswith("rustc "):
        sys.stdout.write(result.stdout)
        raise RuntimeError("-vV reports a version nobody gave it")

    env = dict(os.environ)
    env["RUSTC_OVERRIDE_VERSION_STRING"] = "1.92.0"
    result = run([rustc, "-vV"], env)
    lines = result.stdout.splitlines()
    if result.returncode != 0 or not lines or lines[0] != "rustc 1.92.0" or "release: 1.92.0" not in lines:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError("-vV does not report the overriding version")

    with tempfile.TemporaryDirectory(prefix="trustme-version-override-") as work:
        root = Path(work)
        (root / "lib.rs").write_text(
            "#![feature(no_core, cfg_version)]\n"
            "#![no_core]\n"
            '#[cfg(not(version("1.92")))]\n'
            'compile_error!("cfg(version) does not see the overriding version");\n'
        )
        result = run([
            rustc, "lib.rs", "--crate-type", "lib", "--edition", "2021",
            "-Zstop-after=expand", "-o", str(root / "out"),
        ], env, cwd=work)
        if result.returncode != 0:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise RuntimeError("cfg(version) does not see the overriding version")

    Path(stamp).parent.mkdir(parents=True, exist_ok=True)
    Path(stamp).touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
