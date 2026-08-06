#!/usr/bin/env python3
"""Import Rust 1.90 core/alloc/std unit-test sources as ordinary files.

Usage: import.py /path/to/rust-1.90.0-or-its-library-directory
"""

import re
import shutil
import sys
from bisect import bisect_right
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"


def test_functions(text: str) -> list[tuple[str, tuple[str, ...]]]:
    """Return explicit `#[test] fn name` items, excluding macro templates."""
    lines = text.splitlines(keepends=True)
    offsets = []
    offset = 0
    scopes = []
    stack = []
    for line in lines:
        offsets.append(offset)
        offset += len(line)
        indentation = len(line) - len(line.lstrip(" \t"))
        stripped = line.lstrip()
        if re.fullmatch(r"}[ \t]*(?://[^\n]*)?(?:\n)?", stripped):
            while stack and indentation <= stack[-1][0]:
                stack.pop()
        scopes.append(tuple(name for _, name in stack))
        module = re.match(
            r"(?:pub(?:\([^)]*\))?[ \t]+)?mod[ \t]+"
            r"([A-Za-z_][A-Za-z0-9_]*)[ \t]*\{",
            stripped,
        )
        if module:
            stack.append((indentation, module.group(1)))

    result = []
    for marker in re.finditer(r"^[ \t]*#\s*\[\s*test\s*\][^\n]*", text, re.MULTILINE):
        pos = marker.end()
        while True:
            whitespace = re.match(r"(?:[ \t\r\n]+|//[^\n]*(?:\n|$))*", text[pos:])
            pos += whitespace.end()
            if not text.startswith("#[", pos):
                break
            depth = 0
            while pos < len(text):
                char = text[pos]
                pos += 1
                if char == "[":
                    depth += 1
                elif char == "]":
                    depth -= 1
                    if depth == 0:
                        break

        function = re.match(
            r"(?:pub(?:\([^)]*\))?[ \t]+)?(?:async[ \t]+)?(?:const[ \t]+)?"
            r"fn[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(",
            text[pos:],
        )
        if function:
            line = bisect_right(offsets, marker.start()) - 1
            result.append((function.group(1), scopes[line]))
    return result


def without_function(text: str, name: str) -> str:
    match = re.search(rf"^[ \t]*(?:pub(?:\([^)]*\))?[ \t]+)?fn[ \t]+{name}\b", text, re.MULTILINE)
    if not match:
        return text
    start = text.rfind("\n\n", 0, match.start()) + 2
    brace = text.find("{", match.end())
    if brace < 0:
        return text
    depth = 0
    pos = brace
    while pos < len(text):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                pos += 1
                break
        pos += 1
    return text[:start] + text[pos:]


def preamble(root: Path) -> str:
    text = root.read_text(errors="surrogateescape")
    text = without_function(text, "test_rng")
    return re.sub(
        r"^[ \t]*(?:pub[ \t]+)?mod[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*;[^\n]*$",
        "",
        text,
        flags=re.MULTILINE,
    )


def module_group(tests: Path, source: Path) -> tuple[str, str, str]:
    relative = source.relative_to(tests)
    parts = relative.parts
    if len(parts) == 1:
        group = source.stem
        if source.name == "lib.rs":
            return "lib", "-", ""
        return group, source.relative_to(tests.parent).as_posix(), group

    group = parts[0]
    file_root = tests / f"{group}.rs"
    root = file_root if file_root.is_file() else tests / group / "mod.rs"
    hint_parts = list(parts)
    hint_parts[-1] = source.stem
    if hint_parts[-1] == "mod":
        hint_parts.pop()
    return group, root.relative_to(tests.parent).as_posix(), "::".join(hint_parts)


def std_group(tests: Path, source: Path) -> tuple[str, str, str]:
    relative = source.relative_to(tests)
    parts = relative.parts
    if len(parts) == 1:
        return source.stem, relative.as_posix(), ""
    group = parts[0]
    root = tests / group / "lib.rs"
    hint_parts = list(parts[1:])
    hint_parts[-1] = source.stem
    if hint_parts[-1] == "lib":
        hint_parts.pop()
    return group, root.relative_to(tests).as_posix(), "::".join(hint_parts)


def copy_tree(source: Path, destination: Path) -> None:
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        output = destination / path.relative_to(source)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, output)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: import.py /path/to/rust-1.90.0")
    source = Path(sys.argv[1]).resolve()
    library = source / "library" if (source / "library").is_dir() else source
    suites = {
        "coretests": library / "coretests" / "tests",
        "alloctests": library / "alloctests" / "tests",
        "std": library / "std" / "tests",
    }
    if not all(path.is_dir() for path in suites.values()):
        raise SystemExit(f"missing Rust library test suites under {library}")

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    for suite, tests in suites.items():
        copy_tree(tests, UPSTREAM / suite / "tests")
    copy_tree(library / "alloctests" / "testing", UPSTREAM / "alloctests" / "testing")

    (UPSTREAM / "coretests" / "preamble.rs").write_text(
        preamble(suites["coretests"] / "lib.rs")
    )
    (UPSTREAM / "alloctests" / "preamble.rs").write_text(
        preamble(suites["alloctests"] / "lib.rs")
    )

    cases = []
    groups = {}
    for suite, tests in suites.items():
        for path in sorted(tests.rglob("*.rs")):
            functions = test_functions(path.read_text(errors="surrogateescape"))
            if not functions:
                continue
            if suite == "std":
                group, root, module_hint = std_group(tests, path)
                kind, edition = "crate", "2024"
                source_relative = path.relative_to(tests).as_posix()
                root_relative = f"std/tests/{root}"
            else:
                group, root, module_hint = module_group(tests, path)
                kind = "module"
                edition = "2024" if suite == "coretests" else "2021"
                source_relative = path.relative_to(tests).as_posix()
                root_relative = "-" if root == "-" else f"{suite}/{root}"
            groups[(suite, group)] = (kind, root_relative, edition)
            for name, inline_modules in functions:
                hint = "::".join(
                    part for part in (module_hint, *inline_modules, name) if part
                )
                case = (suite, group, source_relative, name, hint)
                if case not in cases:
                    cases.append(case)

    (HERE / "groups.tsv").write_text(
        "".join(
            f"{suite}\t{group}\t{kind}\t{root}\t{edition}\n"
            for (suite, group), (kind, root, edition) in sorted(groups.items())
        )
    )
    (HERE / "cases.tsv").write_text(
        "".join("\t".join(case) + "\n" for case in cases)
    )
    print(f"imported {len(cases)} explicit library tests in {len(groups)} harness groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
