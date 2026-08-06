#!/usr/bin/env python3
"""Import source-only Rust Book listings verified by Rust 1.90.

Usage: import.py /path/to/rust-book /path/to/rustc
"""

import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import tempfile
import tomllib
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"


def check_case(
    rustc: Path,
    case: tuple[Path, str, str, str, str],
) -> tuple[Path, str, str, str, str] | None:
    crate, relative, root, mode, edition = case
    with tempfile.TemporaryDirectory(prefix="rust-book-import-") as work_name:
        work = Path(work_name)
        shutil.copytree(crate / "src", work / "src")
        source = work / root
        binary = work / "case"
        command = [
            str(rustc),
            "--edition", edition,
            "-C", "linker=clang",
            "-C", "link-arg=-fuse-ld=lld",
            str(source),
            "-o", str(binary),
        ]
        if mode == "test":
            command.append("--test")
        else:
            command.extend(["--crate-type", "bin"])
        compile_result = subprocess.run(
            command,
            cwd=work,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=60,
            check=False,
        )
        if compile_result.returncode != 0:
            return None
        try:
            run_result = subprocess.run(
                [str(binary)],
                cwd=work,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=2,
                check=False,
            )
        except (subprocess.TimeoutExpired, OSError):
            return None
        if run_result.returncode != 0:
            return None
    return case


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    parser.add_argument("rustc", type=Path)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 1))
    args = parser.parse_args()

    listings = (args.checkout / "listings").resolve()
    rustc = args.rustc.resolve()
    if not listings.is_dir():
        parser.error(f"missing Rust Book listings under {args.checkout}")
    if not rustc.is_file():
        parser.error(f"missing reference compiler: {rustc}")

    candidates = []
    for manifest in sorted(listings.rglob("Cargo.toml")):
        crate = manifest.parent
        src = crate / "src"
        if not src.is_dir():
            continue
        cargo = tomllib.loads(manifest.read_text())
        edition = str(cargo.get("package", {}).get("edition", "2015"))
        relative = crate.relative_to(listings).as_posix()
        if (src / "main.rs").is_file():
            candidates.append((crate, relative, "src/main.rs", "run", edition))
        lib = src / "lib.rs"
        has_tests = any(
            re.search(r"#\s*\[\s*test\s*\]", path.read_text(errors="surrogateescape"))
            for path in src.rglob("*.rs")
        )
        if lib.is_file() and has_tests:
            candidates.append((crate, relative, "src/lib.rs", "test", edition))

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        results = executor.map(lambda case: check_case(rustc, case), candidates)
        accepted = [result for result in results if result is not None]

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    copied = set()
    for crate, relative, _root, _mode, _edition in accepted:
        if relative in copied:
            continue
        copied.add(relative)
        for source in sorted((crate / "src").rglob("*.rs")):
            destination = UPSTREAM / relative / source.relative_to(crate)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)

    (HERE / "cases.tsv").write_text(
        "".join(
            f"{relative}\t{root}\t{mode}\t{edition}\n"
            for _crate, relative, root, mode, edition in accepted
        )
    )
    print(f"accepted {len(accepted)} of {len(candidates)} Rust Book targets")
    print(f"copied {len(copied)} listing crates")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
