#!/usr/bin/env python3
"""Reject const_cast in the compiler: every one is a lie about ownership.

Mutation is expressed through a non-const path from the owner, a
build-then-freeze split, or an external cache (see dev/HACKS.md, class B).
"""

import re
import unittest
from pathlib import Path


RUSTC = Path(__file__).parents[2] / "bin" / "rustc"
SOURCE_SUFFIXES = {".cpp", ".h", ".inc"}
CONST_CAST = re.compile(r"\bconst_cast\s*<")


class CompilerNoConstCastTest(unittest.TestCase):
    def test_compiler_has_no_const_cast(self):
        matches = []
        for source in sorted(RUSTC.rglob("*")):
            if source.suffix not in SOURCE_SUFFIXES:
                continue
            for line_number, line in enumerate(source.read_text().splitlines(), 1):
                if CONST_CAST.search(line):
                    matches.append(f"{source.relative_to(RUSTC)}:{line_number}: {line.strip()}")
        self.assertEqual(
            matches,
            [],
            "const_cast found in the compiler; thread a mutable path from "
            "the owner instead:\n" + "\n".join(matches),
        )


if __name__ == "__main__":
    unittest.main()
