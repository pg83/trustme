"""Shared helpers for the test-graph node scripts."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def compiletest_split_flags(flags: str) -> list[str]:
    """Match rustc compiletest's directive parser, including literal `"`."""
    result = []
    for index, part in enumerate(flags.split("'")):
        if index % 2:
            result.append(part)
        else:
            result.extend(part.split())
    return result


MRUSTC_IGNORED_RUSTC_CODEGEN_OPTIONS = {
    "codegen-units",
    "codegen_units",
    "link-dead-code",
    "llvm-args",
    "no-prepopulate-passes",
    "passes",
}


def mrustc_compile_flags(flags: list[str], *, system_rustc: bool) -> list[str]:
    """Drop rustc backend tuning only when running the mrustc adapters.

    Tests whose purpose is the omitted LLVM/CGU behaviour are excluded by the
    corpus importers. The retained tests use these switches only to reproduce a
    language or runtime regression, so passing them to a different backend adds
    no coverage and makes the driver reject otherwise useful input.
    """
    if system_rustc:
        return list(flags)

    result = []
    index = 0
    while index < len(flags):
        flag = flags[index]
        if flag == "-C" and index + 1 < len(flags):
            option = flags[index + 1].split("=", 1)[0]
            if option in MRUSTC_IGNORED_RUSTC_CODEGEN_OPTIONS:
                index += 2
                continue
            result.extend((flag, flags[index + 1]))
            index += 2
            continue
        if flag.startswith("-C") and len(flag) > 2:
            option = flag[2:].split("=", 1)[0]
            if option in MRUSTC_IGNORED_RUSTC_CODEGEN_OPTIONS:
                index += 1
                continue
        result.append(flag)
        index += 1
    return result


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def run(argv, *, cwd=None, env=None, timeout=None) -> None:
    """Run a command, inheriting stdio, raising on failure."""
    subprocess.run(argv, cwd=cwd, env=env, timeout=timeout, check=True)


def wrap_gdb(argv: list[str]) -> list[str]:
    """Prefix a compiler invocation so a signal death is rerun under gdb.

    The wrapper is transparent for normal exits; on SIGSEGV/SIGABRT/... it
    reruns the command under gdb, prints all thread backtraces to stderr and
    re-raises the signal. gdb is assumed to be available.
    """
    wrapper = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wrap_gdb.py")
    return [sys.executable, wrapper, *argv]


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
    search_paths = [release, release / "deps"]
    arguments = []
    for search_path in search_paths:
        if search_path.is_dir():
            arguments.extend(("-L", str(search_path)))
    for crate in crates:
        matches = sorted(
            path
            for search_path in search_paths
            for path in search_path.glob(f"lib{crate}-*.rlib")
        )
        if len(matches) != 1:
            raise RuntimeError(
                f"expected one {crate} rlib under {release}, got {matches}"
            )
        arguments.extend(("--extern", f"{crate}={matches[0]}"))
    return arguments


def workdir():
    """A self-cleaning temporary working directory (use as a context manager)."""
    return tempfile.TemporaryDirectory()
