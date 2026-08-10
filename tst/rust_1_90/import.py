#!/usr/bin/env python3
"""Import self-contained runtime tests from Rust 1.90's UI suite.

Usage: import.py /path/to/rust-1.90.0

The checked-in corpus is intentionally a set of ordinary files, not an archive.
This importer is only a maintainer tool for reproducing the selection.
"""

import re
import shutil
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"
REJECTED_DIRECTIVES = (
    "aux-build",
    "aux-bin",
    "aux-crate",
    "proc-macro",
    "revisions",
    "needs-",
)

HOST_CONDITIONS = {
    "x86_64-unknown-linux-gnu",
    "linux",
    "gnu",
    "x86_64",
    "64bit",
    "unix",
    "elf",
    "nightly",
}


def selected_for_host(text: str) -> bool:
    """Apply compiletest's only-/ignore- rules for our native test target."""
    for match in re.finditer(r"^//@\s*(ignore|only)-([^:\s]+)", text,
                             re.MULTILINE):
        kind, condition = match.groups()
        applies = condition == "test" or condition in HOST_CONDITIONS
        if kind == "ignore" and applies:
            return False
        if kind == "only" and not applies:
            return False
    return True


def selected(text: str) -> bool:
    if not re.search(r"^//@\s*run-pass(?:\s|$)", text, re.MULTILINE):
        return False
    if not re.search(r"\bfn\s+main\s*\(", text):
        return False
    if any(re.search(rf"^//@.*{re.escape(item)}", text, re.MULTILINE)
           for item in REJECTED_DIRECTIVES):
        return False
    if not selected_for_host(text):
        return False
    # These two tests need tests/auxiliary/rust_test_helpers.c and belong with
    # the native/auxiliary import rather than a source-only adapter.
    if re.search(r"^//@\s*compile-flags:.*rust_test_helpers", text,
                 re.MULTILINE):
        return False

    # Keep this first tranche genuinely one-file. Multi-file tests belong in
    # a later adapter with explicit dependency discovery, not in a silently
    # incomplete import.
    if re.search(r"\binclude(?:_str|_bytes)?!\s*\(", text):
        return False
    if re.search(r"#\s*\[\s*path\s*=", text):
        return False
    if re.search(r"^\s*(?:pub\s+)?mod\s+[A-Za-z_][A-Za-z0-9_]*\s*;",
                 text, re.MULTILINE):
        return False
    return True


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: import.py /path/to/rust-1.90.0")
    source = Path(sys.argv[1]).resolve() / "tests" / "ui"
    if not source.is_dir():
        raise SystemExit(f"missing Rust UI suite: {source}")

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)

    cases = []
    for path in sorted(source.rglob("*.rs")):
        text = path.read_text(errors="surrogateescape")
        if not selected(text):
            continue
        relative = path.relative_to(source)
        destination = UPSTREAM / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        cases.append(relative.as_posix())

        base = path.with_suffix("")
        for suffix in (".run.stdout", ".run.stderr"):
            sidecar = Path(str(base) + suffix)
            if sidecar.exists():
                shutil.copyfile(sidecar, Path(str(destination.with_suffix("")) + suffix))

    (HERE / "cases.txt").write_text("".join(f"{case}\n" for case in cases))
    print(f"imported {len(cases)} Rust 1.90 run-pass tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
