#!/usr/bin/env python3

import re
import sys
from pathlib import Path


MARKER = re.compile(
    r"(?<![A-Za-z0-9_])"
    r"(?:TODO|FIX(?:ME)?|HACK|BUG|XXX|KLUDGE|WORKAROUND|WIP|DEBT|UNSOUND)"
    r"(?![A-Za-z0-9_])|escape\s*:",
    re.IGNORECASE,
)
ERROR = "СУКА В ЭТОМ ПРОЕКТЕ ЗАПРЕЩЕНЫ НОВЫЕ КОММЕНТАРИИ"


def is_identifier_char(char: str) -> bool:
    return char.isalnum() or char == "_"


def is_char_literal(text: str, quote: int) -> bool:
    if quote == 0 or not is_identifier_char(text[quote - 1]):
        return True
    for prefix in ("u8", "u", "U", "L"):
        start = quote - len(prefix)
        if start < 0 or text[start:quote] != prefix:
            continue
        if start == 0 or not is_identifier_char(text[start - 1]):
            return True
    return False


def skip_quoted(text: str, start: int, quote: str) -> int:
    cursor = start + 1
    while cursor < len(text):
        if text[cursor] == "\\":
            cursor += 2
        elif text[cursor] == quote:
            return cursor + 1
        else:
            cursor += 1
    return cursor


def skip_raw_string(text: str, start: int) -> int | None:
    if not text.startswith('R"', start):
        return None
    delimiter_end = text.find("(", start + 2, min(start + 19, len(text)))
    if delimiter_end < 0:
        return None
    delimiter = text[start + 2:delimiter_end]
    if any(char.isspace() or char in "()\\" for char in delimiter):
        return None
    close = ")" + delimiter + '"'
    end = text.find(close, delimiter_end + 1)
    return len(text) if end < 0 else end + len(close)


def line_comments(text: str):
    cursor = 0
    line = 1
    while cursor < len(text):
        if text.startswith("//", cursor):
            end = text.find("\n", cursor + 2)
            if end < 0:
                end = len(text)
            yield line, text[cursor + 2:end]
            cursor = end
            continue
        if text.startswith("/*", cursor):
            end = text.find("*/", cursor + 2)
            if end < 0:
                return
            line += text.count("\n", cursor, end + 2)
            cursor = end + 2
            continue
        raw_end = skip_raw_string(text, cursor)
        if raw_end is not None:
            line += text.count("\n", cursor, raw_end)
            cursor = raw_end
            continue
        char = text[cursor]
        if char == '"' or char == "'" and is_char_literal(text, cursor):
            end = skip_quoted(text, cursor, char)
            line += text.count("\n", cursor, end)
            cursor = end
            continue
        if char == "\n":
            line += 1
        cursor += 1


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 3 or args[0] != "--stamp":
        raise SystemExit(f"usage: {sys.argv[0]} --stamp STAMP FILE...")
    stamp = Path(args[1])
    violations = []
    for name in args[2:]:
        path = Path(name)
        for line, comment in line_comments(path.read_text(errors="replace")):
            if not MARKER.search(comment):
                violations.append((path, line))
    if violations:
        for path, line in violations:
            print(f"{path}:{line}: {ERROR}", file=sys.stderr)
        return 1
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
