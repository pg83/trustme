#!/ usr / bin / env python3
"""Compile and run one tst/unit/test_*.rs against a prebuilt libstd, then write
a stamp. Each unit test is its own graph node — a self-contained regression for
one compiler fix that must compile and exit 0.

    run_one.py <test.rs> <libstd.tar> <stamp>

Environment: RUSTC, CC.
"""
import os
import re
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    src = os.path.abspath(sys.argv[1])
    libstd_tar = os.path.abspath(sys.argv[2])
    stamp = os.path.abspath(sys.argv[3])
    rustc = lib.require_env("RUSTC")
    source_text = open(src, encoding="utf-8", errors="surrogateescape").read()
    test_harness = "//@ test-harness" in source_text
    compile_fail_match = re.search(
        r"^//@\s*compile-fail:\s*(.+)$", source_text, re.MULTILINE
    )
    edition_match = re.search(r"^//@\s*edition:\s*(\d+)", source_text, re.MULTILINE)
    edition = edition_match.group(1) if edition_match else "2021"
    compile_flags_match = re.search(
        r"^//@\s*compile-flags:\s*(.*)$", source_text, re.MULTILINE
    )
    compile_flags = (
        shlex.split(compile_flags_match.group(1)) if compile_flags_match else []
    )
    rust_lib_dependencies = "//@ rust-lib-dev-dependencies" in source_text

    with lib.workdir() as work:
        env = dict(os.environ)
        env.setdefault("CC", "cc")

        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        dependency_args = []
        if rust_lib_dependencies:
            dependencies = lib.untar(
                lib.require_env("RUST_LIB_DEPENDENCIES"),
                os.path.join(work, "rust-lib-dependencies"),
            )
            dependency_args = lib.extern_rlib_args(
                dependencies,
                ["rand", "rand_xorshift"],
            )
        binary = os.path.join(work, "t")
        mode = ["--test"] if test_harness else ["--crate-type", "bin"]
        command = [rustc, src, "-L", os.path.join(libstd, "release"), "-o", binary,
                   *mode, "--edition", edition, *dependency_args, *compile_flags]
        if compile_fail_match:
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, check=False)
            expected = compile_fail_match.group(1).encode()
            if result.returncode == 0:
                raise RuntimeError("compiler unexpectedly accepted compile-fail unit")
            if expected not in result.stderr:
                sys.stdout.buffer.write(result.stdout)
                sys.stderr.buffer.write(result.stderr)
                raise RuntimeError(
                    f"compile-fail unit did not emit expected diagnostic: {expected!r}"
                )
        else:
            lib.run(command, env=env)
        if compile_fail_match:
            os.makedirs(os.path.dirname(stamp), exist_ok=True)
            open(stamp, "w").close()
            return 0
        if test_harness:
            listing = subprocess.run([binary, "--list"], env=env,
                                     stdout=subprocess.PIPE, timeout=60,
                                     check=True)
            if b": test" not in listing.stdout:
                raise RuntimeError("test harness contains no tests")
        lib.run([binary], env=env, timeout=60)

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
