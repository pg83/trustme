#!/usr/bin/env python3
"""Import gccrs executable tests as ordinary checked-in files.

Usage: import.py /path/to/gccrs
"""

import shutil
import sys
import re
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: import.py /path/to/gccrs")
    source = (
        Path(sys.argv[1]).resolve()
        / "gcc"
        / "testsuite"
        / "rust"
        / "execute"
    )
    if not source.is_dir():
        raise SystemExit(f"missing gccrs execute suite: {source}")

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)

    # Rust modules and include! inputs are part of the individual tests.  The
    # DejaGNU .exp drivers are replaced by adapter.py and are not vendored.
    files = sorted(source.rglob("*.rs"))
    files.extend(sorted(source.rglob("include.txt")))
    for path in files:
        destination = UPSTREAM / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)

    cases = []
    for path in sorted(source.rglob("*.rs")):
        text = path.read_text(errors="surrogateescape")
        if re.search(r"\bfn\s+main\s*\(", text):
            cases.append(path.relative_to(source).as_posix())

    (HERE / "cases.txt").write_text("".join(f"{case}\n" for case in cases))
    print(f"imported {len(cases)} gccrs execute tests and {len(files) - len(cases)} support files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
