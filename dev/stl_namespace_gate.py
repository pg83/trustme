#!/usr/bin/env python3
"""Reject stl namespace blocks and explicit qualifiers except output specializations."""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} SOURCE.cpp STAMP")

    source = Path(sys.argv[1])
    stamp = Path(sys.argv[2])
    lines = source.read_text(errors="replace").splitlines()
    namespace_hits = [
        line_number
        for line_number, line in enumerate(lines, 1)
        if "namespace stl {" in line
    ]
    qualifier_hits = [
        line_number
        for line_number, line in enumerate(lines, 1)
        if "stl::" in line and not line.startswith("void stl::output<")
    ]
    if namespace_hits or qualifier_hits:
        for line_number in namespace_hits:
            print(
                f"{source}:{line_number}: `namespace stl {{` is forbidden in .cpp files",
                file=sys.stderr,
            )
        for line_number in qualifier_hits:
            print(
                f"{source}:{line_number}: explicit stl:: qualification is forbidden; "
                "use the file-scope `using namespace stl;`",
                file=sys.stderr,
            )
        return 1

    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
