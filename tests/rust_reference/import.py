#!/usr/bin/env python3
"""Extract and reference-check code fences from The Rust Reference.

Usage: import.py /path/to/reference /path/to/rustc
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


def case_mode(info: str, default_edition: str) -> tuple[str, str] | None:
    tokens = [token for token in re.split(r"[\s,]+", info) if token]
    if not tokens or tokens[0].lower() != "rust":
        return None
    edition = default_edition
    for value in ("2015", "2018", "2021", "2024"):
        if f"edition{value}" in tokens:
            edition = value
            break
    if "compile_fail" in tokens:
        return edition, "fail"
    if "no_run" in tokens or "norun" in tokens:
        return edition, "compile"
    if "should_panic" in tokens:
        return edition, "panic"
    return edition, "pass"


def hidden_lines(lines: list[str]) -> list[str]:
    result = []
    for line in lines:
        escaped = re.match(r"^(\s*)##(.*)$", line)
        if escaped:
            result.append(escaped.group(1) + "#" + escaped.group(2))
            continue
        hidden = re.match(r"^(\s*)#(?: |$)(.*)$", line)
        if hidden:
            result.append(hidden.group(1) + hidden.group(2))
            continue
        result.append(line)
    return result


def standalone(lines: list[str]) -> str:
    code = "\n".join(hidden_lines(lines)).strip() + "\n"
    if re.search(r"\bfn\s+main\s*\(", code):
        return code

    attributes = []
    body = []
    for line in code.splitlines():
        if line.lstrip().startswith("#!["):
            attributes.append(line)
        else:
            body.append(line)
    if re.search(r"\?(?=\s*(?:[.;,)\]}]|$))", code):
        final_line = next((line.strip() for line in reversed(body) if line.strip()), "")
        has_result_tail = re.match(r"(?:Ok|Err)(?:\s*::|\s*\()", final_line)
        return "\n".join([
            "#![allow(unused)]",
            *attributes,
            "fn main() {",
            "    fn example() -> Result<(), impl std::fmt::Debug> {",
            *("        " + line for line in body),
            *([] if has_result_tail else ["        Ok(())"]),
            "    }",
            "    example().unwrap();",
            "}",
            "",
        ])
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
    case: tuple[str, str, str, str, str],
) -> tuple[str, str, str, str, str] | None:
    relative, origin, edition, mode, program = case
    with tempfile.TemporaryDirectory(prefix="rust-reference-import-") as work_name:
        work = Path(work_name)
        source = work / "case.rs"
        binary = work / "case"
        source.write_text(program)
        try:
            compile_result = subprocess.run(
                [
                    str(rustc),
                    "--edition", edition,
                    "--crate-type", "bin",
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
        except subprocess.TimeoutExpired:
            return None
        if mode == "fail":
            return case if compile_result.returncode != 0 else None
        if compile_result.returncode != 0:
            return None
        if mode == "compile":
            return case
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
        if mode == "pass" and run_result.returncode != 0:
            return None
        if mode == "panic" and run_result.returncode == 0:
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
        parser.error(f"missing Rust Reference sources under {args.checkout}")
    if not rustc.is_file():
        parser.error(f"missing reference compiler: {rustc}")
    default_edition = tomllib.loads(book_toml.read_text())["rust"]["edition"]

    candidates = []
    for markdown in sorted(source_root.rglob("*.md")):
        source_relative = markdown.relative_to(source_root)
        for line, info, code_lines in fences(markdown.read_text(errors="surrogateescape")):
            settings = case_mode(info, default_edition)
            if settings is None:
                continue
            edition, mode = settings
            filename = f"{source_relative.stem}__L{line}.rs"
            relative = (source_relative.parent / filename).as_posix()
            origin = f"src/{source_relative.as_posix()}:{line}"
            candidates.append((relative, origin, edition, mode, standalone(code_lines)))

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        results = executor.map(lambda case: check_case(rustc, case), candidates)
        accepted = [result for result in results if result is not None]

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    for relative, origin, _edition, _mode, program in accepted:
        destination = UPSTREAM / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(f"// Extracted from {origin}\n{program}")
    (HERE / "cases.tsv").write_text(
        "".join(
            f"{relative}\t{origin}\t{edition}\t{mode}\n"
            for relative, origin, edition, mode, _program in accepted
        )
    )
    print(f"accepted {len(accepted)} of {len(candidates)} Rust Reference fences")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
