#!/usr/bin/env python3
"""Extract and reference-check code fences from The Rustonomicon.

Usage: import.py /path/to/nomicon /path/to/rustc
"""

import argparse
import concurrent.futures
import importlib.util
import os
import shutil
import tomllib
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"
COMMON_PATH = HERE.parent / "rust_reference" / "import.py"


def load_common():
    spec = importlib.util.spec_from_file_location("rust_doc_fence_import", COMMON_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load shared fence importer: {COMMON_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    parser.add_argument("rustc", type=Path)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 1))
    args = parser.parse_args()

    source_root = (args.checkout / "src").resolve()
    rustc = args.rustc.resolve()
    book_toml = args.checkout / "book.toml"
    if not source_root.is_dir() or not book_toml.is_file():
        parser.error(f"missing Rustonomicon sources under {args.checkout}")
    if not rustc.is_file():
        parser.error(f"missing reference compiler: {rustc}")
    default_edition = tomllib.loads(book_toml.read_text())["rust"]["edition"]
    common = load_common()

    recorded = common.recorded_modes(HERE / "cases.tsv")
    candidates = []
    for markdown in sorted(source_root.rglob("*.md")):
        source_relative = markdown.relative_to(source_root)
        for line, info, code_lines in common.fences(
            markdown.read_text(errors="surrogateescape")
        ):
            settings = common.case_mode(info, default_edition)
            if settings is None:
                continue
            edition, mode = settings
            filename = f"{source_relative.stem}__L{line}.rs"
            relative = (source_relative.parent / filename).as_posix()
            origin = f"src/{source_relative.as_posix()}:{line}"
            if recorded.get(relative) == "xfail":
                mode = "xfail"
            candidates.append(
                (relative, origin, edition, mode, common.standalone(code_lines))
            )

    accepted_by_index = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(common.check_case, rustc, case): index
            for index, case in enumerate(candidates)
        }
        for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
            result = future.result()
            if result is not None:
                accepted_by_index[futures[future]] = result
            if completed % 25 == 0 or completed == len(candidates):
                print(f"checked {completed}/{len(candidates)} fences", flush=True)
    accepted = [accepted_by_index[index] for index in sorted(accepted_by_index)]

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    for relative, origin, _edition, _mode, program in accepted:
        destination = UPSTREAM / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(f"// Extracted from {origin}\n{program}")
    (HERE / "cases.tsv").write_text(
        "".join(
            f"{relative}\t{origin}\t{edition}\t{mode}\n"
            for relative, origin, edition, mode, _program in accepted
        )
    )
    print(f"accepted {len(accepted)} of {len(candidates)} Rustonomicon fences")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
