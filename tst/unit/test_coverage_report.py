#!/usr/bin/env python3
"""The local summary, threshold and exported LCOV count the same lines."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "coverage_report", Path(__file__).resolve().parents[2] / "dev" / "coverage.py"
)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class CoverageReportTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
