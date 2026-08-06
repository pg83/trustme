#!/usr/bin/env python3
"""Extract Rust 1.90 library doctests into standalone checked-in programs.

Usage: import.py /path/to/rust-1.90.0-or-its-library-directory
"""

import re
import shutil
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"
CRATES = {"core": "2024", "alloc": "2021", "std": "2024"}


def doc_lines(text: str) -> list[str]:
    result = []
    for line in text.splitlines():
        match = re.match(r"^\s*//[/!] ?(.*)$", line)
        result.append(match.group(1) if match else "")
    return result


def fences(lines: list[str]):
    index = 0
    while index < len(lines):
        opening = re.match(r"^\s*(```+|~~~+)\s*(.*)$", lines[index])
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


def runtime_mode(info: str) -> str | None:
    tokens = [token for token in re.split(r"[\s,]+", info) if token]
    excluded = {"compile_fail", "no_run", "text", "sh", "bash", "toml"}
    if any(token in excluded or token.startswith("ignore") for token in tokens):
        return None
    if tokens:
        first = tokens[0]
        rust_attribute = (
            first == "rust"
            or first == "should_panic"
            or first == "standalone_crate"
            or first.startswith("edition")
        )
        if not rust_attribute:
            return None
    return "panic" if "should_panic" in tokens else "pass"


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


def standalone(crate: str, lines: list[str]) -> str:
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

    prefix = ["#![allow(unused)]", *attributes]
    if crate == "alloc" and not re.search(r"\bextern\s+crate\s+alloc\b", code):
        prefix.append("extern crate alloc;")
    if re.search(r"\?(?=\s*(?:[.;,)\]}]|$))", code):
        final_line = next((line.strip() for line in reversed(body) if line.strip()), "")
        has_result_tail = re.match(r"(?:Ok|Err)(?:\s*::|\s*\()", final_line)
        wrapped = [
            *prefix,
            "fn main() {",
            "    fn doctest() -> Result<(), impl std::fmt::Debug> {",
            *("        " + line for line in body),
            *([] if has_result_tail else ["        Ok(())"]),
            "    }",
            "    doctest().unwrap();",
            "}",
        ]
    else:
        wrapped = [
            *prefix,
            "fn main() {",
            *("    " + line for line in body),
            "}",
        ]
    return "\n".join(wrapped) + "\n"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: import.py /path/to/rust-1.90.0")
    source = Path(sys.argv[1]).resolve()
    library = source / "library" if (source / "library").is_dir() else source
    if not all((library / crate / "src").is_dir() for crate in CRATES):
        raise SystemExit(f"missing core/alloc/std sources under {library}")

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)

    cases = []
    for crate, edition in CRATES.items():
        crate_root = library / crate / "src"
        for source_file in sorted(crate_root.rglob("*.rs")):
            relative = source_file.relative_to(crate_root)
            lines = doc_lines(source_file.read_text(errors="surrogateescape"))
            ordinal = 0
            for line, info, code_lines in fences(lines):
                mode = runtime_mode(info)
                raw_code = "\n".join(code_lines)
                if mode is None:
                    continue
                if not re.search(r"\bassert(?:_eq|_ne)?!\s*\(", raw_code):
                    continue
                if "#[test]" in raw_code:
                    continue
                ordinal += 1
                name = f"{relative.stem}__L{line}_{ordinal}.rs"
                destination = UPSTREAM / crate / relative.parent / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                program = standalone(crate, code_lines)
                origin = f"// Extracted from library/{crate}/src/{relative.as_posix()}:{line}\n"
                destination.write_text(origin + program)
                case = destination.relative_to(UPSTREAM).as_posix()
                source_name = f"{crate}/src/{relative.as_posix()}:{line}"
                cases.append((case, source_name, edition, mode))

    (HERE / "cases.tsv").write_text(
        "".join("\t".join(case) + "\n" for case in cases)
    )
    print(f"imported {len(cases)} Rust 1.90 runtime doctests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
