#!/usr/bin/env python3
"""A crate's release commit need not be on any branch or tag: async-lock
3.4.2 was published from a commit the repository only serves by its id.
Cargo fetches a git dependency's locked revision itself, and the source
node does too when a plain clone does not bring the pinned revision."""

import os
from pathlib import Path
import subprocess
import sys
import tarfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def git(*args: str, cwd: Path) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_git_src_fetches_a_rev_off_every_branch.py GIT_SRC STAMP")
    git_src, stamp_text = sys.argv[1:]

    with lib.workdir() as work_text:
        work = Path(work_text)
        repo = work / "repo"
        repo.mkdir()
        identity = ("-c", "user.name=t", "-c", "user.email=t@t")
        git("init", "-q", "-b", "main", cwd=repo)
        git("config", "uploadpack.allowAnySHA1InWant", "true", cwd=repo)
        (repo / "Cargo.toml").write_text('[package]\nname = "demo"\nversion = "1.0.0"\n')
        git("add", ".", cwd=repo)
        git(*identity, "commit", "-q", "-m", "one", cwd=repo)
        git("checkout", "-q", "-b", "release", cwd=repo)
        (repo / "Cargo.toml").write_text('[package]\nname = "demo"\nversion = "1.0.1"\n')
        git(*identity, "commit", "-q", "-am", "release", cwd=repo)
        release = git("rev-parse", "HEAD", cwd=repo)
        git("checkout", "-q", "main", cwd=repo)
        git("branch", "-q", "-D", "release", cwd=repo)

        archive = work / "src.tar"
        subprocess.run(["python3", git_src, f"file://{repo}", release, str(archive)], check=True)
        with tarfile.open(archive) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("Cargo.toml"))
            text = tar.extractfile(member).read().decode()
        if 'version = "1.0.1"' not in text:
            raise SystemExit(f"archive holds {text!r}, not the release")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
