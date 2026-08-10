#!/usr/bin/env python3
"""Import self-contained Rust 1.90 UI check-pass and build-pass tests.

Usage: import.py /path/to/rust-1.90.0
"""

import json
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import lib  # noqa: E402


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"
REJECTED_DIRECTIVES = (
    "aux-build",
    "aux-crate",
    "proc-macro",
    "revisions",
    "run-flags",
    "needs-",
    "ignore-",
    "only-",
)


def mode(text: str) -> str | None:
    if re.search(r"^//@\s*build-pass(?:\s|$)", text, re.MULTILINE):
        return "build"
    if re.search(r"^//@\s*check-pass(?:\s|$)", text, re.MULTILINE):
        return "check"
    return None


def self_contained(text: str) -> bool:
    if any(
        re.search(rf"^//@.*{re.escape(item)}", text, re.MULTILINE)
        for item in REJECTED_DIRECTIVES
    ):
        return False
    if re.search(r"\binclude(?:_str|_bytes)?!\s*\(", text):
        return False
    if re.search(r"#\s*\[\s*path\s*=", text):
        return False
    if re.search(
        r"^\s*(?:pub\s+)?(?:unsafe\s+)?mod\s+[A-Za-z_][A-Za-z0-9_]*\s*;",
        text,
        re.MULTILINE,
    ):
        return False
    return True


def settings(text: str, test_mode: str) -> tuple[str, str, list[str]]:
    edition_match = re.search(r"^//@\s*edition\s*:\s*(\d+)", text, re.MULTILINE)
    edition = edition_match.group(1) if edition_match else "2015"
    flags = []
    for value in re.findall(r"^//@\s*compile-flags\s*:\s*(.*)$", text, re.MULTILINE):
        flags.extend(lib.compiletest_split_flags(value))
    has_crate_type = any(
        flag == "--crate-type" or flag.startswith("--crate-type=") for flag in flags
    )
    if has_crate_type:
        crate_type = "flags"
    elif test_mode == "check":
        crate_type = "lib"
    elif re.search(r"\bfn\s+main\s*\(", text):
        crate_type = "bin"
    else:
        crate_type = "lib"
    return edition, crate_type, flags


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
        test_mode = mode(text)
        if test_mode is None or not self_contained(text):
            continue
        edition, crate_type, flags = settings(text, test_mode)
        if any("{{" in flag or flag.startswith("@") for flag in flags):
            continue
        relative = path.relative_to(source)
        destination = UPSTREAM / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        cases.append(
            {
                "path": relative.as_posix(),
                "mode": test_mode,
                "edition": edition,
                "crate_type": crate_type,
                "flags": flags,
            }
        )

    (HERE / "cases.json").write_text(json.dumps(cases, indent=2) + "\n")
    counts = {name: sum(case["mode"] == name for case in cases) for name in ("check", "build")}
    print(f"imported {len(cases)} Rust 1.90 positive compile tests")
    print(f"check-pass {counts['check']}, build-pass {counts['build']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
