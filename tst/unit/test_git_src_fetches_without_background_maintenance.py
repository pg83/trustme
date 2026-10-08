#!/usr/bin/env python3
"""After a fetch, git starts its automatic maintenance in the background, and
it can still be writing objects while the source node packs and removes its
checkout: a vendoring node failed that way with "unlinkat .../.git/objects:
directory not empty". Cargo fetches with libgit2 or gix, which maintain
nothing on their own. The source node fetches with that maintenance off."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402

IDENTITY = ("-c", "user.name=t", "-c", "user.email=t@t")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_git_src_fetches_without_background_maintenance.py GIT_SRC STAMP")
    git_src, stamp_text = sys.argv[1:]
    real_git = shutil.which("git")
    if real_git is None:
        raise SystemExit("no git on PATH")

    with lib.workdir() as work_text:
        work = Path(work_text)
        repo = work / "repo"
        repo.mkdir()
        (repo / "Cargo.toml").write_text('[package]\nname = "demo"\nversion = "1.0.0"\n')
        for args in (("init", "-q", "-b", "main"), ("add", "."), ("commit", "-q", "-m", "one")):
            subprocess.run([real_git, *IDENTITY, *args], cwd=repo, check=True, capture_output=True)
        rev = subprocess.run([real_git, "rev-parse", "HEAD"], cwd=repo, check=True, capture_output=True,
                             text=True).stdout.strip()

        wrapper = work / "bin"
        wrapper.mkdir()
        log = work / "git-args"
        (wrapper / "git").write_text(f'#!/bin/sh\necho "$@" >> {log}\nexec {real_git} "$@"\n')
        (wrapper / "git").chmod(0o755)
        env = dict(os.environ, PATH=f"{wrapper}:{os.environ['PATH']}")
        subprocess.run(["python3", git_src, f"file://{repo}", rev, str(work / "src.tar")], check=True, env=env)

        for line in log.read_text().splitlines():
            if " fetch " in f" {line} " and not ("gc.auto=0" in line and "maintenance.auto=false" in line):
                raise SystemExit(f"git fetches with background maintenance on: {line}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
