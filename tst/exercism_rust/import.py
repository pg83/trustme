#!/usr/bin/env python3
"""Import dependency-free Exercism Rust solutions and integration tests.

Usage: import.py /path/to/exercism-rust /path/to/rustc
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import tempfile
import tomllib
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"


def has_dependencies(cargo: dict) -> bool:
    return bool(cargo.get("dependencies") or cargo.get("dev-dependencies"))


def run_command(command: list[str], cwd: Path, timeout: int = 60) -> bool:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except (subprocess.TimeoutExpired, OSError):
        return False
    return result.returncode == 0


def check_case(
    rustc: Path,
    case: tuple[Path, str, str, str, tuple[Path, ...]],
) -> tuple[Path, str, str, str, tuple[Path, ...]] | None:
    exercise, slug, crate_name, edition, tests = case
    with tempfile.TemporaryDirectory(prefix="exercism-rust-import-") as work_name:
        work = Path(work_name)
        solution = work / "solution.rs"
        library = work / f"lib{crate_name}.rlib"
        shutil.copyfile(exercise / ".meta" / "example.rs", solution)
        if not run_command(
            [
                str(rustc),
                "--edition", edition,
                "--crate-name", crate_name,
                "--crate-type", "lib",
                str(solution),
                "-o", str(library),
            ],
            work,
        ):
            return None

        for index, test in enumerate(tests):
            test_source = work / test.name
            binary = work / f"test-{index}"
            shutil.copyfile(test, test_source)
            if not run_command(
                [
                    str(rustc),
                    "--edition", edition,
                    "-C", "linker=clang",
                    "-C", "link-arg=-fuse-ld=lld",
                    "--test", str(test_source),
                    "--extern", f"{crate_name}={library}",
                    "-o", str(binary),
                ],
                work,
            ):
                return None
            if not run_command([str(binary), "--include-ignored"], work, timeout=10):
                return None
    return case


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    parser.add_argument("rustc", type=Path)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 1))
    args = parser.parse_args()

    practice = (args.checkout / "exercises" / "practice").resolve()
    rustc = args.rustc.resolve()
    if not practice.is_dir():
        parser.error(f"missing Exercism Rust practice exercises under {args.checkout}")
    if not rustc.is_file():
        parser.error(f"missing reference compiler: {rustc}")

    candidates = []
    dependency_cases = 0
    for exercise in sorted(path for path in practice.iterdir() if path.is_dir()):
        manifest = exercise / "Cargo.toml"
        solution = exercise / ".meta" / "example.rs"
        tests = tuple(sorted((exercise / "tests").glob("*.rs")))
        if not manifest.is_file() or not solution.is_file() or not tests:
            continue
        cargo = tomllib.loads(manifest.read_text())
        if has_dependencies(cargo):
            dependency_cases += 1
            continue
        package = cargo["package"]
        slug = exercise.name
        crate_name = package["name"].replace("-", "_")
        edition = str(package.get("edition", "2015"))
        candidates.append((exercise, slug, crate_name, edition, tests))

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        results = executor.map(lambda case: check_case(rustc, case), candidates)
        accepted = [result for result in results if result is not None]

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    manifest_rows = []
    for exercise, slug, crate_name, edition, tests in accepted:
        destination = UPSTREAM / slug
        (destination / "tests").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(exercise / ".meta" / "example.rs", destination / "solution.rs")
        for test in tests:
            shutil.copyfile(test, destination / "tests" / test.name)
        manifest_rows.append((slug, crate_name, edition, len(tests)))

    (HERE / "cases.tsv").write_text(
        "".join(
            f"{slug}\t{crate_name}\t{edition}\t{test_files}\n"
            for slug, crate_name, edition, test_files in manifest_rows
        )
    )
    print(f"accepted {len(accepted)} of {len(candidates)} dependency-free exercises")
    print(f"skipped {dependency_cases} exercises with Cargo dependencies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
