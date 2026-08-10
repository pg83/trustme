#!/usr/bin/env python3
"""Reject stale compiler code hidden behind literal-false branches."""

import re
import unittest
from pathlib import Path


RUSTC = Path(__file__).parents[2] / "bin" / "rustc"
SOURCE_SUFFIXES = {".cpp", ".h", ".inc"}
DEAD_BRANCHES = (
    re.compile(r"^\s*#\s*if\s+0(?:\s|$)"),
    re.compile(r"\bif\s*\(\s*false\s*\)"),
)


class CompilerDeadBranchesTest(unittest.TestCase):
    def test_compiler_has_no_literal_false_branches(self):
        matches = []
        for source in sorted(RUSTC.rglob("*")):
            if source.suffix not in SOURCE_SUFFIXES:
                continue
            for line_number, line in enumerate(source.read_text().splitlines(), 1):
                if any(pattern.search(line) for pattern in DEAD_BRANCHES):
                    matches.append(
                        f"{source.relative_to(RUSTC.parent.parent)}:"
                        f"{line_number}: {line.strip()}"
                    )

        self.assertEqual(
            matches,
            [],
            "literal-false branches retain uncompiled compiler code:\n"
            + "\n".join(matches),
        )


if __name__ == "__main__":
    unittest.main()
