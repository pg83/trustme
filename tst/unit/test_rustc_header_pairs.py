#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


RUSTC = Path(__file__).parents[2] / "bin" / "rustc"
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
UPSTREAM_NAME = re.compile(r"mrustc", re.IGNORECASE)


class RustcHeaderPairsTest(unittest.TestCase):
    def test_every_header_has_a_source_that_includes_it_first(self):
        for header in sorted(RUSTC.glob("*.h")):
            with self.subTest(header=header.name):
                source = header.with_suffix(".cpp")
                self.assertTrue(source.is_file(), f"missing {source.name}")
                includes = [
                    match.group(1)
                    for line in source.read_text().splitlines()
                    if (match := INCLUDE.match(line))
                ]
                self.assertTrue(includes, f"{source.name} has no includes")
                self.assertEqual(includes[0], header.name)

    def test_every_source_has_a_header(self):
        for source in sorted(RUSTC.glob("*.cpp")):
            if source.name.endswith("_ut.cpp"):
                # Unit tests pair with the header of the module they test
                continue
            with self.subTest(source=source.name):
                header = source.with_suffix(".h")
                self.assertTrue(header.is_file(), f"missing {header.name}")

    def test_every_unit_test_has_a_module(self):
        for source in sorted(RUSTC.glob("*_ut.cpp")):
            with self.subTest(source=source.name):
                module = source.with_name(source.name.removesuffix("_ut.cpp") + ".h")
                self.assertTrue(module.is_file(), f"missing {module.name}")

    def test_sources_have_no_upstream_branding(self):
        sources = sorted(
            path for path in RUSTC.iterdir() if path.suffix in {".cpp", ".h", ".inc"}
        )
        for source in sources:
            with self.subTest(source=source.name):
                self.assertIsNone(UPSTREAM_NAME.search(source.read_text()))


if __name__ == "__main__":
    unittest.main()
