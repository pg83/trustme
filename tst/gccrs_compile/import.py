#!/usr/bin/env python3
"""Import positive full-compilation tests from the gccrs compile suite.

Usage: import.py /path/to/gccrs
"""

import re
import shutil
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"
DIAGNOSTIC = re.compile(r"dg-(?:error|warning|message|bogus)")


def selected(path: Path, text: str) -> bool:
    if "xfail" in path.parts:
        return False
    if DIAGNOSTIC.search(text):
        return False
    if any(marker in text for marker in ("dg-xfail", "dg-skip-if", "dg-shouldfail")):
        return False
    if "-fsyntax-only" in text:
        return False
    compile_until = re.findall(r"-frust-compile-until=([A-Za-z0-9_-]+)", text)
    if compile_until and compile_until != ["compilation"]:
        return False
    return True


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: import.py /path/to/gccrs")
    source = (
        Path(sys.argv[1]).resolve()
        / "gcc"
        / "testsuite"
        / "rust"
        / "compile"
    )
    if not source.is_dir():
        raise SystemExit(f"missing gccrs compile suite: {source}")

    cases = []
    files = []
    for path in sorted(source.rglob("*.rs")):
        text = path.read_text(errors="surrogateescape")
        if not selected(path, text):
            continue
        cases.append(path.relative_to(source).as_posix())
        files.append(path)

    # A few builtin-macro tests include byte/text fixtures.  Driver .exp files
    # and disabled Rust sources are not compiler inputs.
    files.extend(
        path
        for path in sorted(source.rglob("*"))
        if path.is_file()
        and path.suffix not in (".rs", ".exp", ".disabled")
    )

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    for path in files:
        destination = UPSTREAM / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)

    (HERE / "cases.txt").write_text("".join(f"{case}\n" for case in cases))
    print(f"imported {len(cases)} gccrs full-compilation tests")
    print(f"copied {len(files) - len(cases)} include fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
