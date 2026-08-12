#!/usr/bin/env python3
"""Run a command; on a signal death rerun it under gdb and print every stack.

    wrap_gdb.py <command> [arguments...]

The first run is a plain execution: stdio and the exit status pass through
unchanged, so wrapping is invisible while the command exits normally. If the
command dies from a signal (SIGSEGV, SIGABRT, ...), the same command is rerun
under `gdb -batch` — gdb is assumed to be available — with every thread's
backtrace printed to stderr, and the original signal is then re-raised so the
caller still observes the crash.
"""

import os
import signal
import subprocess
import sys


def print_backtraces(command: list[str]) -> None:
    subprocess.run(
        [
            "gdb",
            "-batch",
            "-nx",
            "-ex", "set confirm off",
            "-ex", "run",
            "-ex", "thread apply all bt",
            "--args",
            *command,
        ],
        stdout=sys.stderr.fileno(),
        check=False,
    )


def main() -> int:
    command = sys.argv[1:]
    if not command:
        raise SystemExit("usage: wrap_gdb.py <command> [arguments...]")

    returncode = subprocess.run(command, check=False).returncode
    if returncode >= 0:
        return returncode

    signum = -returncode
    print(
        f"wrap_gdb: {os.path.basename(command[0])} died with "
        f"{signal.Signals(signum).name}; rerunning under gdb",
        file=sys.stderr,
        flush=True,
    )
    print_backtraces(command)
    if signum != signal.SIGKILL:
        signal.signal(signum, signal.SIG_DFL)
    os.kill(os.getpid(), signum)
    return 128 + signum


if __name__ == "__main__":
    raise SystemExit(main())
