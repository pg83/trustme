#!/usr/bin/env python3
"""A project's sources may sit in git submodules: zstd-safe's zstd-sys
builds the C library from its `zstd` submodule. Cargo checks out a git
source's submodules, recursively, at the commits the superproject records
(`update_submodules` in cargo's sources/git/utils.rs), and the source node
does too."""

import os
from pathlib import Path
import subprocess
import sys
import tarfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402

IDENTITY = ("-c", "user.name=t", "-c", "user.email=t@t", "-c", "protocol.file.allow=always")


def git(*args: str, cwd: Path) -> str:
    return subprocess.run(["git", *IDENTITY, *args], cwd=cwd, check=True, capture_output=True,
                          text=True).stdout.strip()


def repo_with(path: Path, files: dict[str, str]) -> None:
    path.mkdir()
    git("init", "-q", "-b", "main", cwd=path)
    for name, text in files.items():
        (path / name).write_text(text)
    git("add", ".", cwd=path)
    git("commit", "-q", "-m", "one", cwd=path)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_git_src_checks_out_submodules.py GIT_SRC STAMP")
    git_src, stamp_text = sys.argv[1:]

    with lib.workdir() as work_text:
        work = Path(work_text)
        inner = work / "inner"
        repo_with(inner, {"inner.h": "pinned\n"})
        middle = work / "middle"
        repo_with(middle, {"middle.c": "int x;\n"})
        git("submodule", "add", "-q", f"file://{inner}", "inner", cwd=middle)
        git("commit", "-q", "-m", "inner", cwd=middle)
        (inner / "inner.h").write_text("after\n")
        git("commit", "-q", "-am", "after", cwd=inner)

        top = work / "top"
        repo_with(top, {"Cargo.toml": '[package]\nname = "demo"\nversion = "1.0.0"\n'})
        git("submodule", "add", "-q", f"file://{middle}", "sys/middle", cwd=top)
        git("commit", "-q", "-m", "middle", cwd=top)
        rev = git("rev-parse", "HEAD", cwd=top)

        archive = work / "src.tar"
        env = dict(os.environ, GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="protocol.file.allow",
                   GIT_CONFIG_VALUE_0="always")
        subprocess.run(["python3", git_src, f"file://{top}", rev, str(archive)], check=True, env=env)
        with tarfile.open(archive) as tar:
            files = {os.path.normpath(m.name): m for m in tar.getmembers() if m.isfile()}
            for name, want in (("sys/middle/middle.c", "int x;\n"),
                               ("sys/middle/inner/inner.h", "pinned\n")):
                if name not in files:
                    raise SystemExit(f"archive lacks {name}")
                text = tar.extractfile(files[name]).read().decode()
                if text != want:
                    raise SystemExit(f"{name} holds {text!r}, not {want!r}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
