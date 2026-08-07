#!/usr/bin/env python3

import importlib.util
import tempfile
import textwrap
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("rust_doctest_import", HERE / "import.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class ImportTest(unittest.TestCase):
    def test_fence_does_not_cross_non_doc_source(self):
        source = textwrap.dedent(
            """\
            /// ```
            #[doc = concat!("generated setup")]
            /// ```
            /// assert_eq!(1 + 1, 2);
            /// ```
            fn item() {}
            """
        )

        extracted = list(IMPORTER.fences(IMPORTER.doc_lines(source)))

        self.assertEqual(extracted, [(4, "", ["assert_eq!(1 + 1, 2);"])])

    def test_qualified_result_tail_is_not_duplicated(self):
        program = IMPORTER.standalone(
            "std",
            [
                "let value = std::fs::read(\"missing\")?;",
                "# std::io::Result::Ok(())",
            ],
        )

        self.assertEqual(program.count("Ok(())"), 1)

    def test_standalone_does_not_indent_blank_lines(self):
        program = IMPORTER.standalone(
            "std",
            ["let first = 1;", "", "assert_eq!(first, 1);"],
        )

        self.assertFalse(any(line.isspace() for line in program.splitlines()))

    def test_reference_compiler_rejects_invalid_program(self):
        with tempfile.TemporaryDirectory() as directory:
            checker = Path(directory) / "rustc"
            checker.write_text(
                "#!/bin/sh\n"
                "source=$1\n"
                "shift\n"
                "while test $# -gt 0; do\n"
                "  if test \"$1\" = -o; then output=$2; shift 2; else shift; fi\n"
                "done\n"
                "case \"$(cat \"$source\")\" in *INVALID*) exit 1 ;; esac\n"
                "case \"$(cat \"$source\")\" in *PANIC*) status=1 ;; *) status=0 ;; esac\n"
                "printf '#!/bin/sh\\nexit %s\\n' \"$status\" > \"$output\"\n"
                "chmod +x \"$output\"\n"
            )
            checker.chmod(0o755)

            self.assertTrue(
                IMPORTER.reference_accepts(checker, "fn main() {}\n", "2024", "pass")
            )
            self.assertFalse(
                IMPORTER.reference_accepts(checker, "INVALID\n", "2024", "pass")
            )
            self.assertFalse(
                IMPORTER.reference_accepts(checker, "PANIC\n", "2024", "pass")
            )
            self.assertTrue(
                IMPORTER.reference_accepts(checker, "PANIC\n", "2024", "panic")
            )
            self.assertFalse(
                IMPORTER.reference_accepts(checker, "fn main() {}\n", "2024", "panic")
            )


if __name__ == "__main__":
    unittest.main()
