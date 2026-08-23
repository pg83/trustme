#!/usr/bin/env python3
"""Fetch a project's source at a pinned revision and pack it into a tar. This is
a `<proj>_src` graph node.

    git_src.py <url> <rev> <out.tar> [lockfile [lockfile-subdir]]

Set SRC_OVERRIDE to a local checkout to skip the clone.
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lib  # noqa: E402


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
            lib.log(f"[src] cloning {url} @ {rev}")
            lib.run(["git", "clone", "--no-checkout", url, src])
            lib.run(["git", "checkout", "-q", rev], cwd=src)
        if lockfile:
            shutil.copyfile(
                lockfile,
                os.path.join(src, lockfile_subdir, "Cargo.lock"),
            )
        lib.tar_dir(src, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
