#!/usr/bin/env python3
"""Import self-contained native-pass Miri tests compatible with Rust 1.90."""

import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
ASSERTION = re.compile(r"\b(?:debug_)?assert(?:_eq|_ne)?!")


def check_case(rustc: Path, source: Path, pass_root: Path) -> tuple[str, bytes] | None:
    relative = source.relative_to(pass_root).as_posix()
    contents = source.read_bytes()
    text = contents.decode("utf-8", errors="surrogateescape")
    if ASSERTION.search(text) is None:
        return None

    with tempfile.TemporaryDirectory(prefix="miri-import-") as work_name:
        work = Path(work_name)
        isolated_source = work / "case.rs"
        binary = work / "case"
        isolated_source.write_bytes(contents)
        compile_result = subprocess.run(
            [
                str(rustc),
                "--edition", "2021",
                "-C", "linker=/usr/bin/gcc",
                str(isolated_source),
                "-o", str(binary),
            ],
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
                timeout=10,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return None
        if run_result.returncode != 0:
            return None
    return relative, contents


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    parser.add_argument("rustc", type=Path)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 1))
    args = parser.parse_args()

    pass_root = (args.checkout / "tests" / "pass").resolve()
    rustc = args.rustc.resolve()
    if not pass_root.is_dir():
        parser.error(f"missing Miri pass directory: {pass_root}")
    if not rustc.is_file():
        parser.error(f"missing reference compiler: {rustc}")

    candidates = sorted(pass_root.rglob("*.rs"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        results = executor.map(
            lambda source: check_case(rustc, source, pass_root), candidates
        )
        accepted = [result for result in results if result is not None]

    upstream = ROOT / "upstream"
    if upstream.exists():
        shutil.rmtree(upstream)
    upstream.mkdir()
    for relative, contents in accepted:
        destination = upstream / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(contents)
        print(f"imported {relative}")
    (ROOT / "cases.tsv").write_text(
        "".join(f"{relative}\n" for relative, _contents in accepted)
    )
    print(f"accepted {len(accepted)} of {len(candidates)} pass files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
