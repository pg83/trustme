#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path


FETCH = Path(__file__).parents[1] / "std" / "fetch.py"
SPEC = importlib.util.spec_from_file_location("std_fetch", FETCH)
std_fetch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(std_fetch)


class SourceAdjustmentsTest(unittest.TestCase):
    def test_every_edit_is_exact_and_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative, old, _new, expected in std_fetch.SOURCE_EDITS:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(old * expected)

            std_fetch.adjust_sources(directory)

            for relative, old, new, expected in std_fetch.SOURCE_EDITS:
                text = (root / relative).read_text()
                self.assertNotIn(old, text, relative)
                self.assertEqual(text.count(new), expected, relative)

            std_fetch.adjust_sources(directory)


if __name__ == "__main__":
    unittest.main()
