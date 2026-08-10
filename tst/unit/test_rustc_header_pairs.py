#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


RUSTC = Path(__file__).parents[2] / "bin" / "rustc"
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


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
            with self.subTest(source=source.name):
                header = source.with_suffix(".h")
                self.assertTrue(header.is_file(), f"missing {header.name}")


if __name__ == "__main__":
    unittest.main()
