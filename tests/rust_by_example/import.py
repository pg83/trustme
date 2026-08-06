#!/usr/bin/env python3
"""Extract and reference-check standalone Rust By Example programs.

Usage: import.py /path/to/rust-by-example /path/to/rustc
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


def fences(text: str):
    lines = text.splitlines()
    index = 0
    while index < len(lines):
        opening = re.match(r"^\s*(`{3,}|~{3,})\s*(.*)$", lines[index])
        if not opening:
            index += 1
            continue
        marker = opening.group(1)
        info = opening.group(2).strip()
        start_line = index + 2
        index += 1
        code = []
        closing = re.compile(
            r"^\s*" + re.escape(marker[0]) + "{" + str(len(marker)) + r",}\s*$"
        )
        while index < len(lines) and not closing.match(lines[index]):
            code.append(lines[index])
            index += 1
        if index < len(lines):
            yield start_line, info, code
            index += 1


def rust_edition(info: str, default: str) -> str | None:
    tokens = [token for token in re.split(r"[\s,]+", info) if token]
    if not tokens or tokens[0].lower() != "rust":
        return None
    if "compile_fail" in tokens or "no_run" in tokens:
        return None
    for edition in ("2015", "2018", "2021", "2024"):
        if f"edition{edition}" in tokens:
            return edition
    return default


def standalone(lines: list[str]) -> str:
    code = "\n".join(lines).strip() + "\n"
    if re.search(r"\bfn\s+main\s*\(", code):
        return code

    attributes = []
    body = []
    for line in code.splitlines():
        if line.lstrip().startswith("#!["):
            attributes.append(line)
        else:
            body.append(line)
    return "\n".join([
        "#![allow(unused)]",
        *attributes,
        "fn main() {",
        *("    " + line for line in body),
        "}",
        "",
    ])


def check_case(
    rustc: Path,
    case: tuple[str, str, str, str],
) -> tuple[str, str, str, str] | None:
    relative, origin, edition, program = case
    with tempfile.TemporaryDirectory(prefix="rbe-import-") as work_name:
        work = Path(work_name)
        source = work / "case.rs"
        binary = work / "case"
        source.write_text(program)
        compile_result = subprocess.run(
            [
                str(rustc),
                "--edition", edition,
                "-C", "linker=clang",
                "-C", "link-arg=-fuse-ld=lld",
                str(source),
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

    source_root = (args.checkout / "src").resolve()
    rustc = args.rustc.resolve()
    book_toml = args.checkout / "book.toml"
    if not source_root.is_dir() or not book_toml.is_file():
        parser.error(f"missing Rust By Example sources under {args.checkout}")
    if not rustc.is_file():
        parser.error(f"missing reference compiler: {rustc}")
    default_edition = tomllib.loads(book_toml.read_text())["rust"]["edition"]

    candidates = []
    for markdown in sorted(source_root.rglob("*.md")):
        source_relative = markdown.relative_to(source_root)
        for line, info, code_lines in fences(markdown.read_text(errors="surrogateescape")):
            edition = rust_edition(info, default_edition)
            if edition is None:
                continue
            filename = f"{source_relative.stem}__L{line}.rs"
            relative = (source_relative.parent / filename).as_posix()
            origin = f"src/{source_relative.as_posix()}:{line}"
            candidates.append((relative, origin, edition, standalone(code_lines)))

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        results = executor.map(lambda case: check_case(rustc, case), candidates)
        accepted = [result for result in results if result is not None]

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    for relative, origin, edition, program in accepted:
        destination = UPSTREAM / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(f"// Extracted from {origin}\n{program}")
    (HERE / "cases.tsv").write_text(
        "".join(
            f"{relative}\t{origin}\t{edition}\n"
            for relative, origin, edition, _program in accepted
        )
    )
    print(f"accepted {len(accepted)} of {len(candidates)} Rust code fences")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
