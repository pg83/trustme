#!/usr/bin/env python3

import ast
import os
import signal
import subprocess
import sys
import time


def assignment(tree, name):
    for node in tree.body:
        if (
            isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id == name
        ):
            return node.value
    raise RuntimeError(f"assignment is missing: {name}")


def timeout_value(tree, name, script):
    value = assignment(tree, name)
    if not isinstance(value, ast.List):
        raise RuntimeError(f"{name} is not an argv list")
    result = []
    for item in value.elts:
        if isinstance(item, ast.Name) and item.id == "TIMEOUT_SCRIPT":
            result.append(script)
        else:
            result.append(ast.literal_eval(item))
    return result


def test_timeout_script(script):
    result = subprocess.run(
        [sys.executable, script, "1s", sys.executable, "-c", "raise SystemExit(7)"],
        check=False,
    )
    if result.returncode != 7:
        raise RuntimeError(f"timeout changed exit code 7 to {result.returncode}")

    result = subprocess.run(
        [
            sys.executable,
            script,
            "1s",
            sys.executable,
            "-c",
            "import os, signal; os.kill(os.getpid(), signal.SIGTERM)",
        ],
        check=False,
    )
    if result.returncode != -signal.SIGTERM:
        raise RuntimeError(f"timeout changed SIGTERM to {result.returncode}")

    started = time.monotonic()
    result = subprocess.run(
        [sys.executable, script, "0.05s", sys.executable, "-c", "import time; time.sleep(10)"],
        check=False,
    )
    elapsed = time.monotonic() - started
    if result.returncode != 124:
        raise RuntimeError(f"timeout returned {result.returncode}, expected 124")
    if elapsed > 2:
        raise RuntimeError(f"timeout took {elapsed:.2f}s")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_libstd_timeout.py BUILD_PY STAMP")

    build_py, stamp = map(os.path.abspath, sys.argv[1:])
    tree = ast.parse(open(build_py, encoding="utf-8").read(), build_py)

    timeout_script = ast.literal_eval(assignment(tree, "TIMEOUT_SCRIPT"))
    timeout = timeout_value(tree, "LIBSTD_TIMEOUT", timeout_script)
    libstd_commands = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if (
            isinstance(target, ast.Name)
            and target.id == "libstd"
            and isinstance(node.value, ast.Call)
        ):
            libstd_commands.append(node.value)

    expected = ["python3", "$(S)/dev/timeout.py", "10m"]
    if timeout != expected:
        raise RuntimeError(f"libstd timeout differs: {timeout!r} != {expected!r}")
    if not libstd_commands:
        raise RuntimeError("libstd graph command is missing")

    guarded = []
    for command in libstd_commands:
        cmd = next(
            (keyword.value for keyword in command.keywords if keyword.arg == "cmd"),
            None,
        )
        if not isinstance(cmd, ast.List) or not cmd.elts:
            raise RuntimeError("libstd graph command has no argv")
        first = cmd.elts[0]
        if (
            isinstance(first, ast.Starred)
            and isinstance(first.value, ast.Name)
            and first.value.id == "LIBSTD_TIMEOUT"
        ):
            guarded.append(command)
    if len(guarded) != 1:
        raise RuntimeError("libstd graph command is not guarded by LIBSTD_TIMEOUT")

    test_timeout_script(os.path.join(os.path.dirname(build_py), "dev", "timeout.py"))

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
