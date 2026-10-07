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
        for libc in std_fetch.LIBC_EDITS:
            with self.subTest(libc=libc), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "library").mkdir()
                (root / "library" / "Cargo.lock").write_text(
                    f'[[package]]\nname = "libc"\nversion = "{libc}"\n'
                )
                edits = std_fetch.source_edits(directory)
                for relative, old, _new, expected in edits:
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(old * expected)

                std_fetch.adjust_sources(directory)

                for relative, old, new, expected in edits:
                    text = (root / relative).read_text()
                    self.assertNotIn(old, text, relative)
                    self.assertEqual(text.count(new), expected, relative)

                std_fetch.adjust_sources(directory)

    def test_the_libc_edits_follow_the_library_lockfile(self):
        # rustc 1.92's vendor/ holds libc 0.2.155 through 0.2.177; std builds
        # the one library/Cargo.lock pins, and that one loses its build script.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "library").mkdir()
            (root / "library" / "Cargo.lock").write_text(
                '[[package]]\nname = "libc"\nversion = "0.2.177"\n'
            )
            edited = {relative for relative, *_ in std_fetch.source_edits(directory)}
            self.assertIn("vendor/libc-0.2.177/Cargo.toml", edited)
            self.assertFalse(any(relative.startswith("vendor/libc-0.2.174/") for relative in edited))

    def test_the_shim_resolves_against_the_library_lockfile(self):
        # Upstream builds the standard library in the `library/` workspace,
        # against `library/Cargo.lock`. Inside the source tree the shim would
        # otherwise land in the compiler's workspace and its lockfile: rustc
        # 1.92's std asks for libc ^0.2.177 where the compiler locks 0.2.174.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "library").mkdir()
            (root / "library" / "Cargo.lock").write_text('[[package]]\nname = "libc"\nversion = "0.2.177"\n')
            (root / "Cargo.lock").write_text('[[package]]\nname = "libc"\nversion = "0.2.174"\n')

            std_fetch.write_shim(directory)

            manifest = (root / "trustme-stdlib" / "Cargo.toml").read_text()
            self.assertIn("\n[workspace]\n", "\n" + manifest)
            self.assertEqual(
                (root / "trustme-stdlib" / "Cargo.lock").read_text(),
                (root / "library" / "Cargo.lock").read_text(),
            )


if __name__ == "__main__":
    unittest.main()
