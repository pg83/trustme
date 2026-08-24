#!/usr/bin/env python3

import os
import signal
import subprocess
import sys


def parse_duration(value: str) -> float:
    units = {"s": 1, "m": 60, "h": 60 * 60}
    try:
        duration = float(value[:-1]) * units[value[-1]]
    except (KeyError, ValueError):
        raise ValueError(f"invalid duration: {value}") from None
    if duration <= 0:
        raise ValueError(f"duration must be positive: {value}")
    return duration


def signal_self(signum: int) -> int:
    signal.signal(signum, signal.SIG_DFL)
    os.kill(os.getpid(), signum)
    return 128 + signum


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit("usage: timeout.py DURATION COMMAND [ARG ...]")

    try:
        duration = parse_duration(sys.argv[1])
    except ValueError as error:
        raise SystemExit(f"timeout: {error}") from None

    command = sys.argv[2:]
    try:
        process = subprocess.Popen(command, start_new_session=True)
    except FileNotFoundError:
        print(f"timeout: command not found: {command[0]}", file=sys.stderr)
        return 127
    except PermissionError:
        print(f"timeout: cannot execute: {command[0]}", file=sys.stderr)
        return 126

    def forward_signal(signum, _frame):
        try:
            os.killpg(process.pid, signum)
        except ProcessLookupError:
            pass
        signal_self(signum)

    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, forward_signal)

    try:
        returncode = process.wait(timeout=duration)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            pass
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        if process.poll() is None:
            process.wait()
        return 124

    if returncode < 0:
        return signal_self(-returncode)
    return returncode


if __name__ == "__main__":
    raise SystemExit(main())
