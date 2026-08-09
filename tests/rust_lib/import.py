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


def macro_definition_ranges(text: str) -> list[tuple[int, int]]:
    """Return byte ranges occupied by `macro_rules!` definitions."""
    macro = re.compile(
        r"(?<![A-Za-z0-9_])macro_rules[ \t]*![ \t]*"
        r"[A-Za-z_][A-Za-z0-9_]*[ \t]*([({[])"
    )
    ranges = []
    position = 0

    def skip_non_code(index: int) -> int | None:
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            return len(text) if end < 0 else end + 1
        if text.startswith("/*", index):
            depth = 1
            index += 2
            while index < len(text) and depth:
                if text.startswith("/*", index):
                    depth += 1
                    index += 2
                elif text.startswith("*/", index):
                    depth -= 1
                    index += 2
                else:
                    index += 1
            return index

        raw = re.match(r"(?:br|cr|r)(?P<hashes>\#*)\"", text[index:])
        if raw:
            terminator = '"' + raw.group("hashes")
            end = text.find(terminator, index + raw.end())
            return len(text) if end < 0 else end + len(terminator)
        if text[index] == '"':
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                elif text[index] == '"':
                    return index + 1
                else:
                    index += 1
            return index
        character = re.match(r"'(?:\\.|[^\\'\n])'", text[index:])
        if character:
            return index + character.end()
        return None

    while position < len(text):
        skipped = skip_non_code(position)
        if skipped is not None:
            position = skipped
            continue
        found = macro.match(text, position)
        if not found:
            position += 1
            continue

        opening = found.group(1)
        closing = {"(": ")", "[": "]", "{": "}"}[opening]
        depth = 1
        end = found.end()
        while end < len(text) and depth:
            skipped = skip_non_code(end)
            if skipped is not None:
                end = skipped
            elif text[end] == opening:
                depth += 1
                end += 1
            elif text[end] == closing:
                depth -= 1
                end += 1
            else:
                end += 1
        ranges.append((position, end))
        position = end
    return ranges


def test_function_items(text: str) -> list[tuple[str, tuple[str, ...], int]]:
    """Return explicit test names, inline scopes, and marker offsets.

    Tests written inside macro templates are deliberately excluded: they are
    not independently selectable source items.  Their markers are still
    disabled by ``filter_test_source`` when a single explicit test is built.
    """
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

    macro_ranges = macro_definition_ranges(text)
    result = []
    for marker in re.finditer(r"^[ \t]*#\s*\[\s*test\s*\][^\n]*", text, re.MULTILINE):
        if any(start <= marker.start() < end for start, end in macro_ranges):
            continue
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
            result.append((function.group(1), scopes[line], marker.start()))
    return result


def test_functions(text: str) -> list[tuple[str, tuple[str, ...]]]:
    """Return explicit `#[test] fn name` items, excluding macro templates."""
    return [(name, scope) for name, scope, _ in test_function_items(text)]


def filter_test_source(
    text: str,
    *,
    function: str | None = None,
    hint: str = "",
) -> str:
    """Disable every test item except one explicitly selected function.

    The source remains otherwise byte-for-byte unchanged.  This is applied to
    a temporary overlay, never to the imported upstream corpus.  Disabling at
    the attribute level keeps unrelated test bodies out of expansion and
    type checking, so every graph node is an actual single-test compile.
    """
    selected_offset = None
    if function is not None:
        hint_parts = tuple(part for part in hint.split("::") if part)
        candidates = []
        for name, scope, offset in test_function_items(text):
            if name != function:
                continue
            suffix = (*scope, name)
            if len(suffix) <= len(hint_parts) and hint_parts[-len(suffix):] == suffix:
                candidates.append(offset)
        if len(candidates) != 1:
            raise ValueError(
                f"expected one explicit test {hint or function}, got {len(candidates)}"
            )
        selected_offset = candidates[0]

    insertions = []
    for marker in re.finditer(r"^[ \t]*#\s*\[\s*test\s*\][^\n]*", text, re.MULTILINE):
        if marker.start() == selected_offset:
            continue
        line = marker.group(0)
        indentation = line[: len(line) - len(line.lstrip(" \t"))]
        insertions.append((marker.start(), indentation + "#[cfg(any())]\n"))

    for offset, insertion in reversed(insertions):
        text = text[:offset] + insertion + text[offset:]
    return text


def preamble(root: Path) -> str:
    text = root.read_text(errors="surrogateescape")
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
