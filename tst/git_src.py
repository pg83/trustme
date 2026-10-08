#!/usr/bin/env python3
"""Fetch a project's source at a pinned revision and pack it into a tar. This is
a `<proj>_src` graph node.

    git_src.py <url> <rev> <out.tar> [lockfile [lockfile-subdir]]

The pinned revision is fetched alone, by its id, as Cargo fetches a git
dependency's locked revision; a server that refuses that gets a full fetch.
Its submodules are checked out, recursively, at the commits it records, as
Cargo does for a git source.

Set SRC_OVERRIDE to a local checkout to skip the clone.
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib  # noqa: E402

GIT = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false"]


def main() -> int:
    url, rev, out = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
    lockfile = os.path.abspath(sys.argv[4]) if len(sys.argv) > 4 else None
    lockfile_subdir = sys.argv[5] if len(sys.argv) > 5 else "."
    with lib.workdir() as work:
        src = os.path.join(work, "src")
        override = os.environ.get("SRC_OVERRIDE")
        if override:
            shutil.copytree(override, src, symlinks=True)
        else:
            lib.log(f"[src] fetching {url} @ {rev}")
            lib.run([*GIT, "init", "-q", src])
            lib.run([*GIT, "remote", "add", "origin", url], cwd=src)
            shallow = subprocess.run([*GIT, "fetch", "-q", "--depth", "1", "origin", rev],
                                     cwd=src).returncode == 0
            if not shallow:
                lib.run([*GIT, "fetch", "-q", "origin"], cwd=src)
            lib.run([*GIT, "checkout", "-q", rev], cwd=src)
            lib.run([*GIT, "submodule", "update", "-q", "--init", "--recursive", "--depth", "1"],
                    cwd=src)
        if lockfile:
            shutil.copyfile(
                lockfile,
                os.path.join(src, lockfile_subdir, "Cargo.lock"),
            )
        lib.tar_dir(src, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
