#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest import mock


ADAPTER = Path(__file__).parents[1] / "test_project.py"
SPEC = importlib.util.spec_from_file_location("test_project", ADAPTER)
test_project = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(test_project)

TARGETS = [
    {"kind": ["lib"], "name": "widget", "test": True},
    {"kind": ["bin"], "name": "widget-cli", "test": True},
    {"kind": ["bin"], "name": "helper", "test": False},
    {"kind": ["test"], "name": "suite", "test": True},
    {"kind": ["test"], "name": "standalone", "test": True},
    {"kind": ["example"], "name": "demo", "test": True},
    {"kind": ["bench"], "name": "timings", "test": True},
]


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
                manifest="Cargo.toml",
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
                manifest="Cargo.toml",
            )

    def test_without_xfail_preserves_the_original_command(self):
        with mock.patch.object(test_project.lib, "run") as run:
            test_project.run_tests(
                ["cargo", "test"],
                ["--release", "--", "filter"],
                cwd="source",
                env={},
                manifest="Cargo.toml",
            )

        self.assertEqual(
            run.call_args.args[0],
            ["cargo", "test", "--release", "--", "filter"],
        )

    def test_xfail_target_leaves_the_other_targets_selected(self):
        failed = subprocess.CompletedProcess([], 101)
        with (
            mock.patch.object(test_project.lib, "run") as run,
            mock.patch.object(test_project, "package_targets", return_value=TARGETS) as query,
            mock.patch.object(test_project.subprocess, "run", return_value=failed) as raw_run,
        ):
            test_project.run_tests(
                ["cargo", "test"],
                ["--release", "--xfail-target", "standalone"],
                cwd="source",
                env={},
                manifest="Cargo.toml",
            )

        self.assertEqual(query.call_args.args, ("cargo", "Cargo.toml"))
        self.assertEqual(
            [call.args[0] for call in run.call_args_list],
            [
                ["cargo", "test", "--release", "--no-run"],
                [
                    "cargo", "test", "--release",
                    "--lib", "--bin", "widget-cli", "--test", "suite", "--",
                ],
            ],
        )
        self.assertEqual(
            raw_run.call_args.args[0],
            ["cargo", "test", "--release", "--test", "standalone"],
        )

    def test_xfail_target_success_is_an_error(self):
        passed = subprocess.CompletedProcess([], 0)
        with (
            mock.patch.object(test_project.lib, "run"),
            mock.patch.object(test_project, "package_targets", return_value=TARGETS),
            mock.patch.object(test_project.subprocess, "run", return_value=passed),
            self.assertRaisesRegex(
                RuntimeError, "xfail target unexpectedly passed: standalone"
            ),
        ):
            test_project.run_tests(
                ["cargo", "test"],
                ["--xfail-target=standalone"],
                cwd="source",
                env={},
                manifest="Cargo.toml",
            )

    def test_xfail_target_forwards_the_harness_arguments(self):
        failed = subprocess.CompletedProcess([], 101)
        with (
            mock.patch.object(test_project.lib, "run") as run,
            mock.patch.object(test_project, "package_targets", return_value=TARGETS),
            mock.patch.object(test_project.subprocess, "run", return_value=failed) as raw_run,
        ):
            test_project.run_tests(
                ["cargo", "test"],
                [
                    "--xfail", "suite::broken", "--xfail-target=standalone",
                    "--", "--test-threads=1",
                ],
                cwd="source",
                env={},
                manifest="Cargo.toml",
            )

        self.assertEqual(
            run.call_args.args[0],
            [
                "cargo", "test", "--lib", "--bin", "widget-cli", "--test", "suite",
                "--", "--test-threads=1", "--skip", "suite::broken",
            ],
        )
        self.assertEqual(
            [call.args[0] for call in raw_run.call_args_list],
            [
                [
                    "cargo", "test", "--lib", "--bin", "widget-cli", "--test", "suite",
                    "--", "suite::broken", "--exact", "--test-threads=1",
                ],
                ["cargo", "test", "--test", "standalone", "--", "--test-threads=1"],
            ],
        )

    def test_unknown_xfail_target_is_an_error(self):
        with (
            mock.patch.object(test_project.lib, "run"),
            mock.patch.object(test_project, "package_targets", return_value=TARGETS),
            self.assertRaisesRegex(RuntimeError, "no such test target: ghost"),
        ):
            test_project.run_tests(
                ["cargo", "test"],
                ["--xfail-target=ghost"],
                cwd="source",
                env={},
                manifest="Cargo.toml",
            )

    def test_xfail_target_without_a_name_is_an_error(self):
        with self.assertRaisesRegex(RuntimeError, "--xfail-target requires a test target"):
            test_project.split_test_args(["--xfail-target"])

    def test_package_targets_reads_the_manifest_under_test(self):
        metadata = subprocess.CompletedProcess([], 0, stdout=json.dumps({
            "packages": [
                {"manifest_path": "/src/other/Cargo.toml", "targets": []},
                {"manifest_path": "/src/Cargo.toml", "targets": TARGETS},
            ],
        }))
        with mock.patch.object(
            test_project.subprocess, "run", return_value=metadata
        ) as raw_run:
            targets = test_project.package_targets(
                "cargo", "/src/Cargo.toml", cwd="source", env={}
            )

        self.assertEqual(targets, TARGETS)
        self.assertEqual(
            raw_run.call_args.args[0],
            [
                "cargo", "metadata", "--no-deps", "--format-version", "1",
                "--manifest-path", "/src/Cargo.toml",
            ],
        )


if __name__ == "__main__":
    unittest.main()
