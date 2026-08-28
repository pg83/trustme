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

    def test_impl_head_uses_typed_existentials(self):
        source = HELPERS[1].read_text()
        body = source.split("Certainty unifyImplHead(", 1)[1].split(
            "void assembleAliasBoundCandidates", 1
        )[0]
        existential_factory = source.split(
            "const HIRPathParams& implExistentials(", 1
        )[1].split("// ---- Crate-lifetime concrete-goal cache", 1)[0]
        self.assertIn("implExistentials(implParamsDef)", body)
        self.assertIn("HIRGenericRef::newSolverExistential", existential_factory)
        self.assertIn("resolve_.board().id", existential_factory)
        self.assertNotIn("RcString", existential_factory)
        self.assertNotIn("FMT(", existential_factory)
        self.assertNotIn("GENERICPlaceholder", body)
        self.assertNotIn("RcString::newInterned", body)
        self.assertNotIn("impl_?_", body)


if __name__ == "__main__":
    unittest.main()
