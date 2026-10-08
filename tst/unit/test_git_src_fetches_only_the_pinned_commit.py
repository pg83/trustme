#!/usr/bin/env python3
"""The source node needs one revision of a project, not its history:
tokio's or arti's full clone costs minutes of every `projects` run that
changes git_src.py. The pinned commit is fetched alone, by its id."""

import os
from pathlib import Path
import subprocess
import sys
import tarfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402

IDENTITY = ("-c", "user.name=t", "-c", "user.email=t@t")


def git(*args: str, cwd: Path) -> str:
    return subprocess.run(["git", *IDENTITY, *args], cwd=cwd, check=True, capture_output=True,
                          text=True).stdout.strip()


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_git_src_fetches_only_the_pinned_commit.py GIT_SRC STAMP")
    git_src, stamp_text = sys.argv[1:]

    with lib.workdir() as work_text:
        work = Path(work_text)
        repo = work / "repo"
        repo.mkdir()
        git("init", "-q", "-b", "main", cwd=repo)
        for version in ("1.0.0", "1.0.1", "1.0.2"):
            (repo / "Cargo.toml").write_text(f'[package]\nname = "demo"\nversion = "{version}"\n')
            git("add", ".", cwd=repo)
            git("commit", "-q", "-m", version, cwd=repo)
        rev = git("rev-parse", "HEAD~1", cwd=repo)

        archive = work / "src.tar"
        subprocess.run(["python3", git_src, f"file://{repo}", rev, str(archive)], check=True)
        out = work / "out"
        out.mkdir()
        with tarfile.open(archive) as tar:
            tar.extractall(out, filter="tar")
        if 'version = "1.0.1"' not in (out / "Cargo.toml").read_text():
            raise SystemExit("the archive does not hold the pinned revision")
        count = git("rev-list", "--count", "--all", cwd=out)
        if count != "1":
            raise SystemExit(f"the archive's repository holds {count} commits, not the pinned one alone")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
