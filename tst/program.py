"""Execution helpers for non-interactive corpus programs."""

import subprocess


def run(argv, *, cwd=None, env=None, stdout=None, stderr=None,
        timeout=None) -> subprocess.CompletedProcess:
    """Run a test program with a deterministic EOF on stdin."""
    return subprocess.run(
        argv,
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=stdout,
        stderr=stderr,
        timeout=timeout,
        check=False,
    )
