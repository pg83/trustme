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

RUSTC_ONLY_TESTS = {
    "codegen/cfguard-run.rs",
    "functions-closures/parallel-codegen-closures.rs",
    "lifetimes/issue-84604.rs",
    "linking/export-executable-symbols.rs",
    "lto/all-crates.rs",
    "lto/fat-lto.rs",
    "lto/lto-many-codegen-units.rs",
    "lto/thin-lto-global-allocator.rs",
    "lto/thin-lto-inlines.rs",
    "lto/weak-works.rs",
    "sepcomp/sepcomp-fns-backwards.rs",
    "sepcomp/sepcomp-fns.rs",
    "sepcomp/sepcomp-statics.rs",
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
    # `known-bug` cases intentionally assert current rustc miscompilations or
    # otherwise incorrect behavior.  They are upstream bug reproducers, not a
    # conformance corpus for another compiler.
    if re.search(r"^//@\s*known-bug(?:\s|:|$)", text, re.MULTILINE):
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
    # Static-linker/runtime environment tests are not compiler semantics and
    # require a host-specific static libc outside this corpus.
    if re.search(r"^//@\s*compile-flags:.*\+crt-static", text,
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


def ignored_by_compiletest(path: Path, root: Path) -> bool:
    """Honor compiletest-ignore-dir markers in fixture/support directories."""
    for parent in path.parents:
        if parent == root:
            return False
        if (parent / "compiletest-ignore-dir").is_file():
            return True
    return False


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
        if ignored_by_compiletest(path, source):
            continue
        text = path.read_text(errors="surrogateescape")
        if not selected(text):
            continue
        relative = path.relative_to(source)
        if relative.as_posix() in RUSTC_ONLY_TESTS:
            continue
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
