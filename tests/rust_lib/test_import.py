#!/usr/bin/env python3

import importlib.util
import textwrap
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("rust_lib_import", HERE / "import.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class ImportTest(unittest.TestCase):
    def test_macro_template_tests_are_not_imported(self):
        source = textwrap.dedent(
            """\
            mod slice_index {
                macro_rules! panic_cases {
                    () => {
                        mod generated {
                            #[test]
                            fn pass() {}
                        }
                    };
                }

                #[test]
                fn simple() {}

                panic_cases!();
            }
            """
        )

        self.assertEqual(
            IMPORTER.test_functions(source),
            [("simple", ("slice_index",))],
        )


if __name__ == "__main__":
    unittest.main()
