#!/usr/bin/env python3

import ast
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_libstd_timeout.py BUILD_PY STAMP")

    build_py, stamp = map(os.path.abspath, sys.argv[1:])
    tree = ast.parse(open(build_py, encoding="utf-8").read(), build_py)

    timeout = None
    libstd_commands = []
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name) and target.id == "LIBSTD_TIMEOUT":
            timeout = ast.literal_eval(node.value)
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

    expected = ["coreutils", "--coreutils-prog=timeout", "10m"]
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

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
