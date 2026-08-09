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

    def test_filter_keeps_only_selected_nested_test(self):
        source = textwrap.dedent(
            """\
            #[test]
            fn outer() {}

            mod nested {
                #[test]
                #[should_panic]
                fn selected() {}

                #[test]
                fn other() {}
            }
            """
        )

        filtered = IMPORTER.filter_test_source(
            source,
            function="selected",
            hint="group::nested::selected",
        )

        self.assertEqual(filtered.count("#[cfg(any())]"), 2)
        self.assertIn(
            "#[test]\n    #[should_panic]\n    fn selected() {}",
            filtered,
        )
        self.assertIn("#[cfg(any())]\n#[test]\nfn outer() {}", filtered)
        self.assertIn(
            "#[cfg(any())]\n    #[test]\n    fn other() {}",
            filtered,
        )

    def test_filter_disables_tests_inside_macro_templates(self):
        source = textwrap.dedent(
            """\
            macro_rules! generated {
                () => {
                    #[test]
                    fn generated_test() {}
                };
            }

            #[test]
            fn selected() {}
            """
        )

        filtered = IMPORTER.filter_test_source(
            source,
            function="selected",
            hint="selected",
        )

        self.assertEqual(filtered.count("#[cfg(any())]"), 1)
        self.assertIn("#[cfg(any())]\n        #[test]", filtered)

    def test_filter_rejects_missing_test(self):
        with self.assertRaisesRegex(ValueError, "got 0"):
            IMPORTER.filter_test_source(
                "#[test]\nfn present() {}\n",
                function="missing",
                hint="missing",
            )


if __name__ == "__main__":
    unittest.main()
