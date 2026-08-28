#!/usr/bin/env python3
"""Keep deleted next-solver verdict channels from returning."""

import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
HELPERS = (
    ROOT / "bin" / "rustc" / "hir_typeck_helpers.h",
    ROOT / "bin" / "rustc" / "hir_typeck_helpers.cpp",
)
FORBIDDEN = (
    "TraitImplCallback",
    "TraitImplCb",
    "headMatch",
    "aliasRelateActive_",
    "HrtbBoundMatcher",
)


class NextSolverLegacyApiTest(unittest.TestCase):
    def test_deleted_verdict_channels_stay_deleted(self):
        sources = "\n".join(path.read_text() for path in HELPERS)
        for name in FORBIDDEN:
            with self.subTest(name=name):
                self.assertNotIn(name, sources)

    def test_remaining_assembly_callback_has_no_structural_fuzz(self):
        header = HELPERS[0].read_text()
        marker = "struct AssembledImplCallback"
        if marker in header:
            declaration = header.split(marker, 1)[1].split("};", 1)[0]
            self.assertIn("SolverCertainty", declaration)
            self.assertNotIn("HIRCompare", declaration)

if __name__ == "__main__":
    unittest.main()
