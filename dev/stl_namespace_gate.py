#!/usr/bin/env python3
"""Reject explicit stl:: qualifiers except output specializations."""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} SOURCE.cpp STAMP")

    source = Path(sys.argv[1])
    stamp = Path(sys.argv[2])
    hits = [
        line_number
        for line_number, line in enumerate(source.read_text(errors="replace").splitlines(), 1)
        if "stl::" in line and not line.startswith("void stl::output<")
    ]
    if hits:
        for line_number in hits:
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
