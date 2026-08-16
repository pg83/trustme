#!/usr/bin/env python3
"""Check that -Zmir-opt-level selects observable MIR optimisation tiers."""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def compile_mir(rustc: str, src: str, work: str, name: str, flags: list[str]) -> str:
    output = os.path.join(work, name)
    env = dict(os.environ)
    result = subprocess.run(
        lib.wrap_gdb([
            rustc,
            src,
            "--crate-name",
            "mir_opt_level",
            "--crate-type",
            "lib",
            "-o",
            output,
            "-Zdump-mir",
            "-Zstop-after=mir",
            *flags,
        ]),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"MIR compilation {name!r} failed")
    with open(output + "_3_mir.rs", encoding="utf-8") as mir_file:
        return mir_file.read()


def function_body(mir: str, name: str) -> str:
    marker = f'fn ::"mir_opt_level"::{name}('
    start = mir.find(marker)
    if start < 0:
        raise RuntimeError(f"MIR dump has no function {name!r}")
    end = mir.find("\nfn ", start + len(marker))
    return mir[start:] if end < 0 else mir[start:end]


def require_call(mir: str, function: str, callee: str, expected: bool) -> None:
    body = function_body(mir, function)
    needle = f'::"mir_opt_level"::{callee}('
    if (needle in body) != expected:
        state = "retain" if expected else "inline"
        raise RuntimeError(f"{function} must {state} the call to {callee}\n{body}")


def expect_invalid(rustc: str, src: str, work: str) -> None:
    env = dict(os.environ)
    result = subprocess.run(
        lib.wrap_gdb([
            rustc,
            src,
            "--crate-type",
            "lib",
            "-o",
            os.path.join(work, "invalid"),
            "-Zmir-opt-level=not-a-number",
            "-Zstop-after=mir",
        ]),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        raise RuntimeError("invalid -Zmir-opt-level value was accepted")
    if "mir-opt-level" not in result.stderr or "number" not in result.stderr:
        sys.stderr.write(result.stderr)
        raise RuntimeError("invalid value did not produce a numeric mir-opt-level diagnostic")


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_mir_opt_level.py RUSTC INPUT_RS STAMP")

    rustc, src, stamp = map(os.path.abspath, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="trustme-mir-opt-level-") as work:
        level0 = compile_mir(rustc, src, work, "level0", ["-Zmir-opt-level=0"])
        level1 = compile_mir(rustc, src, work, "level1", ["-Z", "mir-opt-level=1"])
        level2 = compile_mir(rustc, src, work, "level2", ["-Zmir-opt-level=2"])
        level3 = compile_mir(rustc, src, work, "level3", ["-Zmir-opt-level=3"])
        level4 = compile_mir(rustc, src, work, "level4", ["-Zmir-opt-level=4"])
        level5 = compile_mir(rustc, src, work, "level5", ["-Zmir-opt-level=5"])
        default = compile_mir(rustc, src, work, "default", [])
        optimized = compile_mir(rustc, src, work, "optimized", ["-O"])
        optimized_level1 = compile_mir(
            rustc, src, work, "optimized_level1", ["-O", "-Zmir-opt-level=1"]
        )

        if "let _$" not in function_body(level0, "copies"):
            raise RuntimeError("MIR level 0 unexpectedly ran local copy propagation")
        if "let _$" in function_body(level1, "copies"):
            raise RuntimeError("MIR level 1 did not run local copy propagation")
        if "retval = _0.0;" not in function_body(level1, "aggregate"):
            raise RuntimeError("MIR level 1 unexpectedly ran aggregate splitting")
        if "retval = a0;" not in function_body(level2, "aggregate"):
            raise RuntimeError("MIR level 2 did not run aggregate splitting")

        require_call(level1, "ordinary_call", "ordinary", True)
        require_call(level2, "ordinary_call", "ordinary", True)
        require_call(level3, "ordinary_call", "ordinary", False)
        require_call(level4, "ordinary_call", "ordinary", False)
        require_call(level5, "ordinary_call", "ordinary", False)
        require_call(default, "ordinary_call", "ordinary", True)
        require_call(optimized, "ordinary_call", "ordinary", False)
        require_call(optimized_level1, "ordinary_call", "ordinary", True)
        expect_invalid(rustc, src, work)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
