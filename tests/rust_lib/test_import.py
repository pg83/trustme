#!/usr/bin/env python3

import importlib.util
import tempfile
import textwrap
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("rust_lib_import", HERE / "import.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)
CASE_SPEC = importlib.util.spec_from_file_location("rust_lib_case", HERE / "case.py")
CASE = importlib.util.module_from_spec(CASE_SPEC)
CASE_SPEC.loader.exec_module(CASE)


class ImportTest(unittest.TestCase):
    def test_preamble_preserves_non_test_helpers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "lib.rs"
            root.write_text(
                textwrap.dedent(
                    """\
                    mod selected;

                    fn test_rng() -> u32 {
                        42
                    }
                    """
                ),
                encoding="utf-8",
            )

            preamble = IMPORTER.preamble(root)

            self.assertNotIn("mod selected;", preamble)
            self.assertIn("fn test_rng() -> u32", preamble)

    def test_module_harness_preserves_nested_module_lookup(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = CASE.prepare_source(
                Path(temporary),
                HERE / "upstream",
                "coretests",
                "ops",
                "module",
                "coretests/tests/ops.rs",
                "ops/control_flow.rs",
                "control_flow_discriminants_match_result",
                "ops::control_flow::control_flow_discriminants_match_result",
            )

            self.assertEqual(
                source,
                Path(temporary) / "source/upstream/coretests/tests/lib.rs",
            )
            wrapper = source.read_text(encoding="utf-8")
            self.assertIn("\nmod ops;\n", wrapper)
            self.assertNotIn("#[path =", wrapper)
            self.assertTrue(source.parent.joinpath("ops/control_flow.rs").is_file())

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
