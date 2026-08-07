#!/usr/bin/env python3
"""Compile and run shards of dependency-free Exercism Rust exercises."""

import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def fail_output(result: subprocess.CompletedProcess, case: str, stage: str) -> int:
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    print(f"FAIL Exercism {case}: {stage} exit {result.returncode}", file=sys.stderr)
    return result.returncode or 1


def main() -> int:
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: adapter.py CASES START COUNT UPSTREAM LIBSTD_TAR STAMP"
        )
    cases_path, start_text, count_text, upstream, libstd_tar, stamp = sys.argv[1:]
    start = int(start_text)
    count = int(count_text)
    upstream = os.path.abspath(upstream)
    libstd_tar = os.path.abspath(libstd_tar)
    stamp = os.path.abspath(stamp)
    rustc = lib.require_env("RUSTC")

    cases = [line.split("\t") for line in open(cases_path).read().splitlines()]
    selected = cases[start:start + count]
    if len(selected) != count:
        raise SystemExit(f"manifest contains no complete shard at row {start}")

    environment = dict(os.environ)
    environment["MRUSTC_TARGET_VER"] = "1.90"
    environment.setdefault("CC", "cc")
    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        library_path = os.path.join(libstd, "release")
        for index, (slug, crate_name, edition, test_count_text) in enumerate(selected):
            print(f"[Exercism Rust] {slug}", file=sys.stderr, flush=True)
            exercise = os.path.join(upstream, slug)
            library = os.path.join(work, f"lib-{index}.rlib")
            compile_library = subprocess.run(
                [
                    rustc,
                    os.path.join(exercise, "solution.rs"),
                    "-L", library_path,
                    "-o", library,
                    "--crate-name", crate_name,
                    "--crate-type", "lib",
                    "--edition", edition,
                ],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if compile_library.returncode != 0:
                return fail_output(compile_library, slug, "solution compile")

            tests_root = os.path.join(exercise, "tests")
            test_sources = sorted(
                os.path.join(tests_root, name)
                for name in os.listdir(tests_root)
                if name.endswith(".rs")
            )
            if len(test_sources) != int(test_count_text):
                raise SystemExit(f"test-file count changed for {slug}")
            for test_index, test_source in enumerate(test_sources):
                binary = os.path.join(work, f"test-{index}-{test_index}")
                compile_test = subprocess.run(
                    [
                        rustc,
                        test_source,
                        "-L", library_path,
                        "-o", binary,
                        "--test",
                        "--edition", edition,
                        "--extern", f"{crate_name}={library}",
                    ],
                    env=environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                )
                if compile_test.returncode != 0:
                    return fail_output(compile_test, slug, "test compile")
                try:
                    run_result = subprocess.run(
                        [binary, "--include-ignored"],
                        cwd=work,
                        env=environment,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        timeout=60,
                        check=False,
                    )
                except subprocess.TimeoutExpired:
                    print(f"FAIL Exercism {slug}: timed out after 60 seconds", file=sys.stderr)
                    return 1
                if run_result.returncode != 0:
                    return fail_output(run_result, slug, "runtime")

    os.makedirs(os.path.dirname(stamp), exist_ok=True)
    open(stamp, "w").close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
