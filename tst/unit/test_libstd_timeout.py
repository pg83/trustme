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


def budget_call(tree, name):
    value = assignment(tree, name)
    if (
        not isinstance(value, ast.Call)
        or not isinstance(value.func, ast.Name)
        or value.func.id != "budget"
    ):
        raise RuntimeError(f"{name} is not a budget() call")
    return {
        keyword.arg: ast.literal_eval(keyword.value) for keyword in value.keywords
    }


def budget_runs_timeout_script(tree):
    """True when budget() spends its duration through TIMEOUT_SCRIPT."""
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == "budget":
            return any(
                isinstance(inner, ast.Name) and inner.id == "TIMEOUT_SCRIPT"
                for inner in ast.walk(node)
            )
    raise RuntimeError("budget() is missing")


def timeout_scale(tree):
    """The multiplier build.py gives a sanitized toolchain."""
    value = assignment(tree, "TIMEOUT_SCALE")
    if not isinstance(value, ast.IfExp):
        raise RuntimeError("TIMEOUT_SCALE is not a conditional")
    return ast.literal_eval(value.body), ast.literal_eval(value.orelse)


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
    if timeout_script != "$(S)/dev/timeout.py":
        raise RuntimeError(f"timeout script differs: {timeout_script!r}")
    if not budget_runs_timeout_script(tree):
        raise RuntimeError("budget() does not run the timeout script")
    timeout = budget_call(tree, "LIBSTD_TIMEOUT")
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

    expected = {"minutes": 10}
    if timeout != expected:
        raise RuntimeError(f"libstd timeout differs: {timeout!r} != {expected!r}")
    sanitized, plain = timeout_scale(tree)
    if plain != 1:
        raise RuntimeError(f"an unsanitized build stretches its budgets by {plain}")
    if sanitized <= 1:
        raise RuntimeError(f"a sanitized build stretches its budgets by {sanitized}")
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
