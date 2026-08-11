#!/usr/bin/env python3

import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "tst" / "system_rustc.py"


class SystemRustcModeTest(unittest.TestCase):
    def test_launcher_enables_unstable_features(self):
        with tempfile.TemporaryDirectory() as work_text:
            work = Path(work_text)
            compiler = work / "compiler"
            compiler.write_text(
                "#!/usr/bin/env python3\n"
                "import os\n"
                "print(os.environ.get('RUSTC_BOOTSTRAP', ''))\n"
            )
            compiler.chmod(0o755)
            launcher = work / "rustc"
            subprocess.run(
                [
                    sys.executable,
                    HELPER,
                    "launcher",
                    compiler,
                    "/bin/true",
                    launcher,
                ],
                check=True,
            )
            result = subprocess.run(
                [launcher], stdout=subprocess.PIPE, text=True, check=True
            )
            self.assertEqual(result.stdout.strip(), "1")

    def test_empty_libstd_has_release_directory(self):
        with tempfile.TemporaryDirectory() as work_text:
            archive = Path(work_text) / "libstd.tar"
            subprocess.run(
                [sys.executable, HELPER, "empty-libstd", archive], check=True
            )
            with tarfile.open(archive) as source:
                self.assertIn("./release", source.getnames())

    def test_build_graph_substitutes_rustc_without_resvg(self):
        with tempfile.TemporaryDirectory() as build_dir:
            result = subprocess.run(
                [
                    ROOT / "build",
                    "-B",
                    build_dir,
                    "-Dsystem_rustc=/bin/true",
                    "-Dsystem_cargo=/bin/true",
                    "-G",
                    "test",
                ],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                text=True,
                check=True,
            )
        graph = json.loads(result.stdout)
        exports = {entry["name"]: entry for entry in graph["exports"]}
        self.assertEqual(exports["rustc"]["kind"], "command")
        nodes = graph["nodes"]
        rustc_node = next(node for node in nodes if node["id"] == exports["rustc"]["node"])
        self.assertIn("$(S)/tst/system_rustc.py", rustc_node["inputs"])
        self.assertNotIn("resvg", exports)
        self.assertFalse(
            any("$(B)/tst/resvg.stamp" in node["outputs"] for node in nodes)
        )

        gccrs_nodes = [
            node
            for node in nodes
            if any("$(B)/tst/gccrs/" in output for output in node["outputs"])
        ]
        gccrs_compile_nodes = [
            node
            for node in nodes
            if any("$(B)/tst/gccrs_compile/" in output for output in node["outputs"])
        ]
        self.assertEqual(len(gccrs_nodes), 301)
        self.assertEqual(len(gccrs_compile_nodes), 570)
        for node in (*gccrs_nodes, *gccrs_compile_nodes):
            self.assertIn("$(B)/tst/libstd.tar", node["inputs"])
            self.assertTrue(
                any("$(B)/tst/libstd.tar" in command for command in node["cmd"])
            )


if __name__ == "__main__":
    unittest.main()
