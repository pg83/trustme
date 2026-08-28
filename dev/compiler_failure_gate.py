#!/usr/bin/env python3

import argparse
import pathlib
import re
import sys


RUNTIME_ERROR_MAX = {
    "bin/rustc/expand_cfg.cpp": 1,
    "bin/rustc/hir_serialise_lowlevel.cpp": 8,
    "bin/rustc/memory_dump.cpp": 2,
    "bin/rustc/parse_lex.cpp": 3,
    "bin/rustc/path.cpp": 2,
    "bin/rustc/toml.cpp": 22,
}


def tokens(text):
    i = 0
    line = 1
    size = len(text)
    raw_re = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(')
    while i < size:
        if text.startswith("//", i):
            end = text.find("\n", i + 2)
            i = size if end < 0 else end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                line += text.count("\n", i)
                return
            line += text.count("\n", i, end + 2)
            i = end + 2
            continue

        raw = raw_re.match(text, i)
        if raw:
            marker = ")" + raw.group(1) + '"'
            end = text.find(marker, raw.end())
            end = size if end < 0 else end + len(marker)
            line += text.count("\n", i, end)
            i = end
            continue

        char = text[i]
        if char in "\"'":
            quote = char
            i += 1
            while i < size:
                if text[i] == "\\":
                    if i + 1 < size and text[i + 1] == "\n":
                        line += 1
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                if text[i] == "\n":
                    line += 1
                i += 1
            continue
        if char.isspace():
            if char == "\n":
                line += 1
            i += 1
            continue
        if char.isalpha() or char == "_":
            end = i + 1
            while end < size and (text[end].isalnum() or text[end] == "_"):
                end += 1
            yield text[i:end], line
            i = end
            continue
        if text.startswith("::", i):
            yield "::", line
            i += 2
            continue
        yield char, line
        i += 1


def check(path):
    text = path.read_text(encoding="utf-8")
    errors = []
    for line, source in enumerate(text.splitlines(), 1):
        if re.match(
            r"\s*#\s*include\s*[<\"](?:cassert|assert\.h)[>\"]", source
        ):
            errors.append((line, "assert header is forbidden; use BUG_ASSERT"))

    source_tokens = list(tokens(text))
    runtime_errors = 0
    for index, (token, line) in enumerate(source_tokens):
        if (
            token == "assert"
            and index + 1 < len(source_tokens)
            and source_tokens[index + 1][0] == "("
        ):
            errors.append(
                (line, "assert is forbidden; use BUG_ASSERT, ASSERT_BUG, or BUG")
            )
        sequence = [entry[0] for entry in source_tokens[index : index + 4]]
        if sequence == ["throw", "std", "::", "runtime_error"]:
            runtime_errors += 1

    path_text = path.as_posix()
    marker = "bin/rustc/"
    marker_index = path_text.find(marker)
    source_name = path_text[marker_index:] if marker_index >= 0 else path_text
    maximum = RUNTIME_ERROR_MAX.get(source_name, 0)
    if runtime_errors > maximum:
        errors.append(
            (
                0,
                f"std::runtime_error count {runtime_errors} exceeds allowed "
                f"{maximum}; internal failures must use BUG",
            )
        )
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stamp", required=True)
    parser.add_argument("sources", nargs="+")
    args = parser.parse_args()

    failed = False
    for source in args.sources:
        path = pathlib.Path(source)
        for line, message in check(path):
            location = f"{path}:{line}" if line else str(path)
            print(f"{location}: {message}", file=sys.stderr)
            failed = True
    if failed:
        return 1

    stamp = pathlib.Path(args.stamp)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text("")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
