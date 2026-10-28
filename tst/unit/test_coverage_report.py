#!/usr/bin/env python3
"""The local summary, threshold and exported LCOV count the same lines."""

import importlib.util
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "coverage_report", Path(__file__).resolve().parents[2] / "dev" / "coverage.py"
)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "dev"))
import coverage_branches


class CoverageReportTest(unittest.TestCase):
    def test_branches_ignore_comments_and_strings(self):
        source = '''if (ready) {
            const char* text = R"tag(if (fake) { })tag";
            /* else { not a branch } */
            if (inner) { run(); }
        } else { stop(); }
        switch (value) { case Kind::One: { one(); } default: { other(); } }
        '''
        conditions = [source[start:opening].strip()
                      for start, opening, _, _ in coverage_branches.branches(source)]
        self.assertEqual(conditions, ["if (ready)", "if (inner)", "else", "switch (value)",
                                     "case Kind::One:", "default:"])

    def test_loop_headers_handle_nested_parentheses(self):
        source = '''for (auto item : values(filter())) { use(item); }
        for (int i = 0; i < limit(); ++i) { step(i); }
        while (next(value)) { use(value); }
        do { step(); } while (again());
        '''
        conditions = [source[start:opening].strip()
                      for start, opening, _, _ in coverage_branches.branches(source)]
        self.assertEqual(conditions, ["for (auto item : values(filter()))",
                                     "for (int i = 0; i < limit(); ++i)",
                                     "while (next(value))", "do"])

    def test_rank_uses_uncovered_body_lines_without_counting_nested_branches_twice(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "bin/rustc/example.cpp"
            source.parent.mkdir(parents=True)
            source.write_text("if (a) {\n if (b) {\n  missed();\n }\n}\n"
                              "else {\n hit();\n}\n")
            trace = root / "coverage.info"
            trace.write_text("SF:bin/rustc/example.cpp\nDA:1,1\nDA:2,0\nDA:3,0\n"
                             "DA:4,0\nDA:7,1\nend_of_record\n")
            ranked = coverage_branches.rank(root, trace, minimum=1)
            self.assertEqual(len(ranked), 1)
            self.assertEqual((ranked[0]["line"], ranked[0]["end_line"]), (1, 5))
            self.assertEqual(ranked[0]["uncovered_lines"], 3)

    def test_totals_follow_line_records_and_exclude_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "bin/rustc/example.cpp"
            source.parent.mkdir(parents=True)
            source.touch()
            raw = (
                "SF:bin/rustc/example.cpp\nDA:10,4\nDA:11,0\n"
                "LF:100\nLH:99\nend_of_record\n"
                "SF:ext/libstd/example.cpp\nDA:1,1\nLF:1\nLH:1\nend_of_record\n"
            )
            with patch.object(report, "run_output", return_value=raw):
                records = report.export_lcov(root, [source], [], root / "coverage.info")
            self.assertEqual(len(records), 1)
            self.assertIn("LF:2\nLH:1\n", records[0])
            self.assertNotIn("ext/libstd", (root / "coverage.info").read_text())
            report.check_floor(records, 50)
            with self.assertRaisesRegex(RuntimeError, "50.00% is below 90.00%"):
                report.check_floor(records, 90)
            report.write_summary(records, root / "summary.txt")
            total = (root / "summary.txt").read_text().splitlines()[-1].split()
            self.assertEqual(total, ["TOTAL", "2", "1", "1", "50.00%"])

    def test_duplicate_line_records_count_once(self):
        self.assertEqual(report.line_counts("DA:7,0\nDA:7,3\nDA:8,0\n"), (1, 2))

    def test_empty_coverage_cannot_pass(self):
        with self.assertRaisesRegex(RuntimeError, "no instrumented lines"):
            report.check_floor(["SF:empty.cpp\nLF:10\nLH:10\n"], 0)

    def test_variants_union_lines_and_preserve_uncovered_code(self):
        records = report.union_lines([
            "SF:a.cpp\nFN:1,release\nDA:2,1\nDA:3,0\nDA:4,0\nLF:90\nLH:90\n",
            "SF:a.cpp\nFN:1,debug\nDA:2,3\nDA:3,7\nDA:5,0\nDA:6,1\n",
            "SF:b.cpp\nDA:1,0\n",
        ])
        self.assertEqual(records, [
            "SF:a.cpp\nDA:2,4\nDA:3,7\nDA:4,0\nDA:5,0\nDA:6,1\nLF:5\nLH:3\nend_of_record\n",
            "SF:b.cpp\nDA:1,0\nLF:1\nLH:0\nend_of_record\n",
        ])
        report.check_floor(records, 50)
        with self.assertRaisesRegex(RuntimeError, "50.00% is below 90.00%"):
            report.check_floor(records, 90)


if __name__ == "__main__":
    unittest.main()
