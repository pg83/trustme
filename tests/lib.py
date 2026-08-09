"""Shared helpers for the test-graph node scripts."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def run(argv, *, cwd=None, env=None, timeout=None) -> None:
    """Run a command, inheriting stdio, raising on failure."""
    subprocess.run(argv, cwd=cwd, env=env, timeout=timeout, check=True)


def require_env(name: str) -> str:
    val = os.environ.get(name)
    if not val:
        sys.exit(f"{name} is not set")
    return val


def mrustc_link(work: str) -> str:
    """Give the graph-built compiler a stable `mrustc` basename and return it."""
    rustc = os.path.realpath(require_env("RUSTC"))
    bindir = os.path.join(work, "bin")
    os.makedirs(bindir, exist_ok=True)
    link = os.path.join(bindir, "mrustc")
    if os.path.lexists(link):
        os.remove(link)
    os.symlink(rustc, link)
    return link


def untar(archive: str, dest: str, *, zstd: bool = False) -> str:
    os.makedirs(dest, exist_ok=True)
    argv = ["tar", "-C", dest, "-x"]
    if zstd:
        argv.append("--zstd")
    argv += ["-f", archive]
    run(argv)
    return dest


def tar_dir(src: str, out: str, *, zstd: bool = False) -> None:
    argv = ["tar", "-C", src, "-c"]
    if zstd:
        argv.append("--zstd")
    argv += ["-f", out, "."]
    run(argv)


def extern_rlib_args(root: str, crates: list[str]) -> list[str]:
    """Return `-L`/`--extern` arguments for uniquely built Cargo rlibs."""
    release = Path(root) / "release"
    arguments = ["-L", str(release)]
    for crate in crates:
        matches = sorted(release.glob(f"lib{crate}-*.rlib"))
        if len(matches) != 1:
            raise RuntimeError(
                f"expected one {crate} rlib under {release}, got {matches}"
            )
        arguments.extend(("--extern", f"{crate}={matches[0]}"))
    return arguments


def workdir():
    """A self-cleaning temporary working directory (use as a context manager)."""
    return tempfile.TemporaryDirectory()
