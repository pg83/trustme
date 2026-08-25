#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest import mock


ADAPTER = Path(__file__).parents[1] / "test_project.py"
SPEC = importlib.util.spec_from_file_location("test_project", ADAPTER)
test_project = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(test_project)


class ProjectXfailTest(unittest.TestCase):
    def test_xfail_failure_passes_after_other_tests(self):
        failed = subprocess.CompletedProcess([], 101)
        with (
            mock.patch.object(test_project.lib, "run") as run,
            mock.patch.object(test_project.subprocess, "run", return_value=failed) as raw_run,
        ):
            test_project.run_tests(
                ["cargo", "test"],
                ["--release", "--xfail", "suite::broken"],
                cwd="source",
                env={},
            )

        self.assertEqual(
            [call.args[0] for call in run.call_args_list],
            [
                ["cargo", "test", "--release", "--no-run"],
                [
                    "cargo", "test", "--release", "--",
                    "--skip", "suite::broken",
                ],
            ],
        )
        self.assertEqual(
            raw_run.call_args.args[0],
            [
                "cargo", "test", "--release", "--",
                "suite::broken", "--exact",
            ],
        )

    def test_xfail_success_is_an_error(self):
        passed = subprocess.CompletedProcess([], 0)
        with (
            mock.patch.object(test_project.lib, "run"),
            mock.patch.object(test_project.subprocess, "run", return_value=passed),
            self.assertRaisesRegex(RuntimeError, "xfail unexpectedly passed"),
        ):
            test_project.run_tests(
                ["cargo", "test"],
                ["--xfail=suite::fixed"],
                cwd="source",
                env={},
            )

    def test_without_xfail_preserves_the_original_command(self):
        with mock.patch.object(test_project.lib, "run") as run:
            test_project.run_tests(
                ["cargo", "test"],
                ["--release", "--", "filter"],
                cwd="source",
                env={},
            )

        self.assertEqual(
            run.call_args.args[0],
            ["cargo", "test", "--release", "--", "filter"],
        )


if __name__ == "__main__":
    unittest.main()
