#!/usr/bin/env python3
"""Compile and run one exact Rust 1.90 library test."""

import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("rust_lib_import", HERE / "import.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)

FEATURE_ATTRIBUTE_RE = re.compile(
    r"#!\s*\[\s*feature\s*\((.*?)\)\s*\]",
    re.DOTALL,
)


def populate_overlay(
    source: Path,
    destination: Path,
    selected_source: Path,
    function: str,
    hint: str,
    *,
    strip_selected_features: bool = False,
) -> None:
    """Mirror one suite and disable tests other than the selected one."""
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        if path.suffix != ".rs":
            output.symlink_to(path.resolve())
            continue

        text = path.read_text(encoding="utf-8", errors="surrogateescape")
        if relative == selected_source:
            filtered = IMPORTER.filter_test_source(
                text,
                function=function,
                hint=hint,
            )
            if strip_selected_features:
                filtered = FEATURE_ATTRIBUTE_RE.sub("", filtered)
        else:
            filtered = IMPORTER.filter_test_source(text)
        # Keep every Rust source physically in the overlay.  Module lookup must
        # stay inside it even when a source file has no test markers itself.
        output.write_text(filtered, encoding="utf-8", errors="surrogateescape")


def selected_preamble(
    path: Path,
    source: str,
    function: str,
    hint: str,
) -> str:
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    if source == "lib.rs":
        return IMPORTER.filter_test_source(text, function=function, hint=hint)
    return IMPORTER.filter_test_source(text)


def feature_names(text: str) -> list[str]:
    attributes = FEATURE_ATTRIBUTE_RE.findall(text)
    return [
        name.strip()
        for features in attributes
        for name in features.split(",")
        if name.strip()
    ]


def selected_features(path: Path, crate_preamble: str) -> str:
    """Lift only module feature gates absent from the harness crate."""
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    existing = set(feature_names(crate_preamble))
    lifted = []
    for name in feature_names(text):
        if name not in existing:
            existing.add(name)
            lifted.append(name)
    return "".join(f"#![feature({name})]\n" for name in lifted)


def prepare_source(
    work: Path,
    upstream: Path,
    suite: str,
    group: str,
    kind: str,
    root: str,
    source: str,
    function: str,
    hint: str,
) -> Path:
    source_root = work / "source"
    overlay_upstream = source_root / "upstream"
    overlay_suite = overlay_upstream / suite
    populate_overlay(
        upstream / suite,
        overlay_suite,
        Path("tests") / source,
        function,
        hint,
        strip_selected_features=kind not in ("crate", "adapter-crate"),
    )

    overlay_adapter = source_root / "adapter"
    populate_overlay(HERE / "adapter", overlay_adapter, Path("-"), function, hint)

    if kind == "crate":
        return overlay_upstream / root
    if kind == "adapter-crate":
        return overlay_adapter / root

    if kind not in (
        "module",
        "adapter-module",
        "module-shard",
        "module-support",
    ):
        raise ValueError(f"unknown harness kind: {kind}")

    wrapper = selected_preamble(
        upstream / suite / "preamble.rs",
        source,
        function,
        hint,
    )
    wrapper = selected_features(
        upstream / suite / "tests" / source,
        wrapper,
    ) + wrapper
    wrapper_path = overlay_suite / "tests" / "lib.rs"
    if suite == "alloctests":
        wrapper += "\nmod testing;\n"

    if root != "-":
        support = None
        if kind in ("module-shard", "module-support"):
            root, support = root.split("|", 1)
            support_path = overlay_adapter / support
            wrapper += "\n" + support_path.read_text(
                encoding="utf-8",
                errors="surrogateescape",
            )
        if kind == "adapter-module":
            module = overlay_adapter / root
        else:
            module = overlay_upstream / root
        default_file = wrapper_path.parent / f"{group}.rs"
        default_module = wrapper_path.parent / group / "mod.rs"
        if module == default_file or module == default_module:
            wrapper += f"\nmod {group};\n"
        else:
            module_path = Path(os.path.relpath(module, wrapper_path.parent)).as_posix()
            wrapper += f"\n#[path = {json.dumps(module_path)}]\nmod {group};\n"

    wrapper_path.write_text(wrapper, encoding="utf-8", errors="surrogateescape")
    return wrapper_path


def select_harness_test(listing: bytes, function: str, hint: str) -> str:
    names = []
    for line in listing.decode("utf-8", errors="surrogateescape").splitlines():
        if line.endswith(": test"):
            raw_name = line.removesuffix(": test")
            names.append((raw_name.removeprefix("::"), raw_name))
    candidates = [
        (name, raw_name)
        for name, raw_name in names
        if name.rsplit("::", 1)[-1] == function
    ]
    exact = [raw_name for name, raw_name in candidates if name == hint]
    if len(exact) == 1:
        return exact[0]
    if len(candidates) == 1:
        return candidates[0][1]
    raise ValueError(f"expected one harness test for {hint}, got {candidates}")


def main() -> int:
    if len(sys.argv) != 13:
        raise SystemExit(
            "usage: case.py SUITE GROUP KIND ROOT EDITION SOURCE FUNCTION HINT "
            "UPSTREAM LIBSTD_TAR DEPENDENCIES_TAR STAMP"
        )
    (
        suite,
        group,
        kind,
        root,
        edition,
        source,
        function,
        hint,
        upstream_text,
        libstd_tar_text,
        dependencies_tar_text,
        stamp_text,
    ) = sys.argv[1:]
    upstream = Path(upstream_text).resolve()
    libstd_tar = Path(libstd_tar_text).resolve()
    dependencies_tar = Path(dependencies_tar_text).resolve()
    stamp = Path(stamp_text).resolve()
    rustc = lib.require_env("RUSTC")
    case = f"{suite}/{source}::{hint}"
    print(f"[Rust library test] {case}", file=sys.stderr, flush=True)

    environment = dict(os.environ)
    environment.setdefault("CC", "cc")
    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(str(libstd_tar), str(work / "libstd")))
        dependencies = lib.untar(
            str(dependencies_tar),
            str(work / "rust-lib-dependencies"),
        )
        source_path = prepare_source(
            work,
            upstream,
            suite,
            group,
            kind,
            root,
            source,
            function,
            hint,
        )
        binary = work / "test"
        crate_name = (
            Path(source).stem if kind in ("crate", "adapter-crate") else suite
        )
        result = subprocess.run(
            [
                rustc,
                str(source_path),
                "-L", str(libstd / "release"),
                "-o", str(binary),
                "--test",
                "--edition", edition,
                *lib.extern_rlib_args(
                    dependencies,
                    ["cfg_if", "rand", "rand_xorshift"],
                ),
                "--crate-name", re.sub(
                    r"[^A-Za-z0-9_]",
                    "_",
                    crate_name,
                ),
            ],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != 0:
            sys.stdout.buffer.write(result.stdout)
            sys.stderr.buffer.write(result.stderr)
            return result.returncode

        listing = subprocess.run(
            [str(binary), "--list"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=600,
            check=False,
        )
        if listing.returncode != 0:
            sys.stdout.buffer.write(listing.stdout)
            sys.stderr.buffer.write(listing.stderr)
            return listing.returncode or 1
        try:
            selected = select_harness_test(listing.stdout, function, hint)
        except ValueError as error:
            print(f"FAIL {case}: {error}", file=sys.stderr)
            return 1

        try:
            result = subprocess.run(
                [str(binary), selected, "--exact", "--include-ignored", "--nocapture"],
                timeout=600,
                check=False,
            )
        except subprocess.TimeoutExpired:
            print(f"FAIL {case}: timed out after 600 seconds", file=sys.stderr)
            return 1
        if result.returncode != 0:
            return result.returncode or 1

    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
