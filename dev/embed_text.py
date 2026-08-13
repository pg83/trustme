#!/usr/bin/env python3
"""Embed a text file in a generated C++ header without runtime file access."""

import hashlib
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: embed_text.py INPUT OUTPUT SYMBOL")

    input_path = pathlib.Path(sys.argv[1])
    output_path = pathlib.Path(sys.argv[2])
    symbol = sys.argv[3]
    text = input_path.read_text(encoding="utf-8")
    delimiter = "MRUSTC_" + hashlib.sha256(text.encode()).hexdigest()[:8]
    terminator = ")" + delimiter + '"'
    if terminator in text:
        raise RuntimeError("generated raw-string delimiter occurs in input")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        "#pragma once\n"
        f"static constexpr char {symbol}[] = R\"{delimiter}("
        f"{text}){delimiter}\";\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
