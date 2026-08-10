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
    def test_committed_manifest_partitions_target_excluded_tests(self):
        cases = {
            tuple(line.split("\t"))
            for line in (HERE / "cases.tsv").read_text().splitlines()
        }
        excluded = {
            tuple(line.split("\t")[:5])
            for line in (HERE / "excluded_cases.tsv").read_text().splitlines()
        }

        self.assertTrue(cases.isdisjoint(excluded))
        self.assertIn(
            ("coretests", "mem", "mem.rs", "size_of_64", "mem::size_of_64"),
            cases,
        )
        self.assertIn(
            ("coretests", "mem", "mem.rs", "size_of_32", "mem::size_of_32"),
            excluded,
        )
        self.assertIn(
            (
                "coretests",
                "floats",
                "floats/f16.rs",
                "test_abs",
                "floats::f16::test_abs",
            ),
            excluded,
        )
        self.assertIn(
            ("std", "run-time-detect", "run-time-detect.rs", "x86_all", "x86_all"),
            cases,
        )
        self.assertIn(
            ("std", "run-time-detect", "run-time-detect.rs", "arm_linux", "arm_linux"),
            excluded,
        )

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

    def test_commented_and_literal_test_markers_are_not_imported(self):
        source = textwrap.dedent(
            '''\
            /*
            #[test]
            fn commented() {}
            */
            const TEXT: &str = r#"
            #[test]
            fn string_contents() {}
            "#;
            #[test]
            fn real() {}
            '''
        )

        self.assertEqual(IMPORTER.test_functions(source), [("real", ())])

    def test_expanded_harness_locations_are_read_from_libtest_descriptors(self):
        expanded = textwrap.dedent(
            """\
            #[rustc_test_marker =
            "mem::size_of_64"]
            pub const size_of_64: test::TestDescAndFn = test::TestDescAndFn {
                desc: test::TestDesc {
                    source_file: "/checkout/library/coretests/tests/mem.rs",
                    start_line: 31usize,
                },
            };
            #[rustc_test_marker = "external::generated"]
            pub const generated: test::TestDescAndFn = test::TestDescAndFn {
                desc: test::TestDesc {
                    source_file: "/checkout/library/portable-simd/test.rs",
                    start_line: 9usize,
                },
            };
            """
        )

        self.assertEqual(
            IMPORTER.expanded_test_locations(expanded, "coretests"),
            {("coretests/tests/mem.rs", 31)},
        )

    def test_only_tests_exported_by_the_host_harness_are_selected(self):
        records = [
            (("coretests", "mem", "mem.rs", "size_of_32", "mem::size_of_32"), [24]),
            (("coretests", "mem", "mem.rs", "size_of_64", "mem::size_of_64"), [31]),
        ]
        locations = {
            "coretests": {("coretests/tests/mem.rs", 31)},
        }

        selected, excluded = IMPORTER.select_target_applicable_cases(records, locations)

        self.assertEqual(selected, [records[1][0]])
        self.assertEqual(
            excluded,
            [(*records[0][0], "24", "absent-from-target-harness")],
        )

    def test_backend_capability_cfgs_come_from_the_target_driver(self):
        source = textwrap.dedent(
            """\
            #![feature(cfg_target_has_reliable_f16_f128)]
            #[cfg(target_has_reliable_f16)]
            fn f16_test() {}
            #[cfg(target_thread_local)]
            fn tls_test() {}
            """
        )

        rewritten = IMPORTER.rewrite_target_capability_cfg(source, set())

        self.assertIn("feature(cfg_target_has_reliable_f16_f128)", rewritten)
        self.assertIn("cfg(trustme_disabled_target_has_reliable_f16)", rewritten)
        self.assertIn("cfg(trustme_disabled_target_thread_local)", rewritten)

    def test_net_shards_keep_the_identity_of_applicable_upstream_tests(self):
        cases = [
            (
                "coretests",
                "net",
                "net/ip_addr.rs",
                "test_from_str_ipv4",
                "net::ip_addr::test_from_str_ipv4",
            ),
        ]
        groups = {
            ("coretests", "net"): ("module", "coretests/tests/net/mod.rs", "2024"),
            ("std", "sync"): ("crate", "std/tests/sync/lib.rs", "2024"),
        }

        adapted, adapted_groups = IMPORTER.apply_harness_layout(cases, groups)

        self.assertEqual(
            adapted,
            [
                (
                    "coretests",
                    "net_ip_0",
                    "net/ip_addr.rs",
                    "test_from_str_ipv4",
                    "net_ip_0::test_from_str_ipv4",
                ),
            ],
        )
        self.assertEqual(adapted_groups[("std", "sync")][0], "adapter-crate")

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

    def test_filter_keeps_cfg_variants_with_the_same_test_name(self):
        source = textwrap.dedent(
            """\
            #[test]
            #[cfg(target_pointer_width = "32")]
            fn pointer_width() {}

            #[test]
            #[cfg(target_pointer_width = "64")]
            fn pointer_width() {}

            #[test]
            fn other() {}
            """
        )

        filtered = IMPORTER.filter_test_source(
            source,
            function="pointer_width",
            hint="pointer_width",
        )

        self.assertEqual(filtered.count("#[cfg(any())]"), 1)


if __name__ == "__main__":
    unittest.main()
