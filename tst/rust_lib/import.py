#!/usr/bin/env python3
"""Import target-applicable Rust 1.90 core/alloc/std unit tests.

Usage: import.py /path/to/rust-1.90.0 --rustc /path/to/rustc-1.90.0
                 --target-rustc /path/to/mrustc-driver
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from bisect import bisect_right
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


HERE = Path(__file__).resolve().parent
UPSTREAM = HERE / "upstream"
RUST_COMMIT = "1159e78c4747b02ef996e55082b704c09b970588"
SUITES = ("coretests", "alloctests", "std")
TARGET_CAPABILITY_CFGS = {
    "target_has_reliable_f16",
    "target_has_reliable_f16_math",
    "target_has_reliable_f128",
    "target_has_reliable_f128_math",
    "target_thread_local",
}


def macro_definition_ranges(text: str) -> list[tuple[int, int]]:
    """Return byte ranges occupied by `macro_rules!` definitions."""
    macro = re.compile(
        r"(?<![A-Za-z0-9_])macro_rules[ \t]*![ \t]*"
        r"[A-Za-z_][A-Za-z0-9_]*[ \t]*([({[])"
    )
    ranges = []
    position = 0

    def skip_non_code(index: int) -> int | None:
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            return len(text) if end < 0 else end + 1
        if text.startswith("/*", index):
            depth = 1
            index += 2
            while index < len(text) and depth:
                if text.startswith("/*", index):
                    depth += 1
                    index += 2
                elif text.startswith("*/", index):
                    depth -= 1
                    index += 2
                else:
                    index += 1
            return index

        raw = re.match(r"(?:br|cr|r)(?P<hashes>\#*)\"", text[index:])
        if raw:
            terminator = '"' + raw.group("hashes")
            end = text.find(terminator, index + raw.end())
            return len(text) if end < 0 else end + len(terminator)
        if text[index] == '"':
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                elif text[index] == '"':
                    return index + 1
                else:
                    index += 1
            return index
        character = re.match(r"'(?:\\.|[^\\'\n])'", text[index:])
        if character:
            return index + character.end()
        return None

    while position < len(text):
        skipped = skip_non_code(position)
        if skipped is not None:
            position = skipped
            continue
        found = macro.match(text, position)
        if not found:
            position += 1
            continue

        opening = found.group(1)
        closing = {"(": ")", "[": "]", "{": "}"}[opening]
        depth = 1
        end = found.end()
        while end < len(text) and depth:
            skipped = skip_non_code(end)
            if skipped is not None:
                end = skipped
            elif text[end] == opening:
                depth += 1
                end += 1
            elif text[end] == closing:
                depth -= 1
                end += 1
            else:
                end += 1
        ranges.append((position, end))
        position = end
    return ranges


def non_code_ranges(text: str) -> list[tuple[int, int]]:
    """Return comment and literal ranges that cannot contain Rust attributes."""
    ranges = []
    position = 0
    while position < len(text):
        start = position
        if text.startswith("//", position):
            end = text.find("\n", position + 2)
            position = len(text) if end < 0 else end + 1
            ranges.append((start, position))
            continue
        if text.startswith("/*", position):
            depth = 1
            position += 2
            while position < len(text) and depth:
                if text.startswith("/*", position):
                    depth += 1
                    position += 2
                elif text.startswith("*/", position):
                    depth -= 1
                    position += 2
                else:
                    position += 1
            ranges.append((start, position))
            continue

        raw = re.match(r'(?:br|cr|r)(?P<hashes>\#*)"', text[position:])
        if raw:
            terminator = '"' + raw.group("hashes")
            end = text.find(terminator, position + raw.end())
            position = len(text) if end < 0 else end + len(terminator)
            ranges.append((start, position))
            continue
        string = re.match(r'(?:b|c)?"', text[position:])
        if string:
            position += string.end()
            while position < len(text):
                if text[position] == "\\":
                    position += 2
                elif text[position] == '"':
                    position += 1
                    break
                else:
                    position += 1
            ranges.append((start, position))
            continue
        character = re.match(r"(?:b)?'(?:\\.|[^\\'\n])'", text[position:])
        if character:
            position += character.end()
            ranges.append((start, position))
            continue
        position += 1
    return ranges


def offset_in_ranges(offset: int, ranges: list[tuple[int, int]]) -> bool:
    starts = [start for start, _ in ranges]
    index = bisect_right(starts, offset) - 1
    return index >= 0 and offset < ranges[index][1]


def test_function_items(text: str) -> list[tuple[str, tuple[str, ...], int, int]]:
    """Return explicit test names, scopes, marker offsets, and function lines.

    Tests written inside macro templates are deliberately excluded: they are
    not independently selectable source items.  Their markers are still
    disabled by ``filter_test_source`` when a single explicit test is built.
    """
    lines = text.splitlines(keepends=True)
    offsets = []
    offset = 0
    scopes = []
    stack = []
    for line in lines:
        offsets.append(offset)
        offset += len(line)
        indentation = len(line) - len(line.lstrip(" \t"))
        stripped = line.lstrip()
        if re.fullmatch(r"}[ \t]*(?://[^\n]*)?(?:\n)?", stripped):
            while stack and indentation <= stack[-1][0]:
                stack.pop()
        scopes.append(tuple(name for _, name in stack))
        module = re.match(
            r"(?:pub(?:\([^)]*\))?[ \t]+)?mod[ \t]+"
            r"([A-Za-z_][A-Za-z0-9_]*)[ \t]*\{",
            stripped,
        )
        if module:
            stack.append((indentation, module.group(1)))

    macro_ranges = macro_definition_ranges(text)
    ignored_ranges = non_code_ranges(text)
    result = []
    for marker in re.finditer(r"^[ \t]*#\s*\[\s*test\s*\][^\n]*", text, re.MULTILINE):
        if offset_in_ranges(marker.start(), macro_ranges) or offset_in_ranges(
            marker.start(), ignored_ranges
        ):
            continue
        pos = marker.end()
        while True:
            whitespace = re.match(r"(?:[ \t\r\n]+|//[^\n]*(?:\n|$))*", text[pos:])
            pos += whitespace.end()
            if not text.startswith("#[", pos):
                break
            depth = 0
            while pos < len(text):
                char = text[pos]
                pos += 1
                if char == "[":
                    depth += 1
                elif char == "]":
                    depth -= 1
                    if depth == 0:
                        break

        function = re.match(
            r"(?:pub(?:\([^)]*\))?[ \t]+)?(?:async[ \t]+)?(?:const[ \t]+)?"
            r"fn[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(",
            text[pos:],
        )
        if function:
            line = bisect_right(offsets, marker.start()) - 1
            function_offset = pos + function.start()
            function_line = bisect_right(offsets, function_offset)
            result.append(
                (function.group(1), scopes[line], marker.start(), function_line)
            )
    return result


def test_functions(text: str) -> list[tuple[str, tuple[str, ...]]]:
    """Return explicit `#[test] fn name` items, excluding macro templates."""
    return [(name, scope) for name, scope, _, _ in test_function_items(text)]


def filter_test_source(
    text: str,
    *,
    function: str | None = None,
    hint: str = "",
) -> str:
    """Disable every test item except one explicitly selected function.

    The source remains otherwise byte-for-byte unchanged.  This is applied to
    a temporary overlay, never to the imported upstream corpus.  Disabling at
    the attribute level keeps unrelated test bodies out of expansion and
    type checking, so every graph node is an actual single-test compile.
    """
    selected_offsets = set()
    if function is not None:
        hint_parts = tuple(part for part in hint.split("::") if part)
        candidates = []
        for name, scope, offset, _ in test_function_items(text):
            if name != function:
                continue
            suffix = (*scope, name)
            if len(suffix) <= len(hint_parts) and hint_parts[-len(suffix):] == suffix:
                candidates.append(offset)
        if not candidates:
            raise ValueError(
                f"expected an explicit test {hint or function}, got 0"
            )
        selected_offsets.update(candidates)

    insertions = []
    for marker in re.finditer(r"^[ \t]*#\s*\[\s*test\s*\][^\n]*", text, re.MULTILINE):
        if marker.start() in selected_offsets:
            continue
        line = marker.group(0)
        indentation = line[: len(line) - len(line.lstrip(" \t"))]
        insertions.append((marker.start(), indentation + "#[cfg(any())]\n"))

    for offset, insertion in reversed(insertions):
        text = text[:offset] + insertion + text[offset:]
    return text


def preamble(root: Path) -> str:
    text = root.read_text(errors="surrogateescape")
    return re.sub(
        r"^[ \t]*(?:pub[ \t]+)?mod[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*;[^\n]*$",
        "",
        text,
        flags=re.MULTILINE,
    )


def module_group(tests: Path, source: Path) -> tuple[str, str, str]:
    relative = source.relative_to(tests)
    parts = relative.parts
    if len(parts) == 1:
        group = source.stem
        if source.name == "lib.rs":
            return "lib", "-", ""
        return group, source.relative_to(tests.parent).as_posix(), group

    group = parts[0]
    file_root = tests / f"{group}.rs"
    root = file_root if file_root.is_file() else tests / group / "mod.rs"
    hint_parts = list(parts)
    hint_parts[-1] = source.stem
    if hint_parts[-1] == "mod":
        hint_parts.pop()
    return group, root.relative_to(tests.parent).as_posix(), "::".join(hint_parts)


def std_group(tests: Path, source: Path) -> tuple[str, str, str]:
    relative = source.relative_to(tests)
    parts = relative.parts
    if len(parts) == 1:
        return source.stem, relative.as_posix(), ""
    group = parts[0]
    root = tests / group / "lib.rs"
    hint_parts = list(parts[1:])
    hint_parts[-1] = source.stem
    if hint_parts[-1] == "lib":
        hint_parts.pop()
    return group, root.relative_to(tests).as_posix(), "::".join(hint_parts)


def copy_tree(source: Path, destination: Path) -> None:
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        output = destination / path.relative_to(source)
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, output)


def expanded_test_locations(text: str, suite: str) -> set[tuple[str, int]]:
    """Return `(suite/tests/path.rs, fn_line)` entries emitted by libtest."""
    marker_re = re.compile(
        r'#\[rustc_test_marker\s*=\s*"[^"]+"\s*\]',
        re.MULTILINE,
    )
    markers = list(marker_re.finditer(text))
    locations = set()
    needle = f"{suite}/tests/"
    for index, marker in enumerate(markers):
        end = markers[index + 1].start() if index + 1 < len(markers) else len(text)
        descriptor = text[marker.end():end]
        source = re.search(r'source_file:\s*"([^"]+)"', descriptor)
        line = re.search(r"start_line:\s*([0-9]+)usize", descriptor)
        if source is None or line is None:
            raise ValueError("rustc test marker has no source location")
        source_file = source.group(1)
        offset = source_file.rfind(needle)
        if offset < 0:
            continue
        locations.add((source_file[offset:], int(line.group(1))))
    return locations


def run_expansion(
    command: list[str],
    *,
    cwd: Path,
    environment: dict[str, str],
) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="surrogateescape",
        check=False,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"harness expansion failed: {' '.join(command)}")
    if result.stderr:
        sys.stderr.write(result.stderr)
    return result.stdout


def exact_toolchain(source_root: Path, rustc: Path) -> Path:
    version = subprocess.run(
        [str(rustc), "-Vv"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=True,
    ).stdout
    if "release: 1.90.0" not in version or f"commit-hash: {RUST_COMMIT}" not in version:
        raise SystemExit(f"expected rustc 1.90.0 commit {RUST_COMMIT}, got:\n{version}")

    commit_file = source_root / "git-commit-hash"
    if not commit_file.is_file() or commit_file.read_text().strip() != RUST_COMMIT:
        raise SystemExit(f"source tree is not Rust commit {RUST_COMMIT}")

    cargo = rustc.parent / "cargo"
    if not cargo.is_file():
        raise SystemExit(f"cargo from the rustc 1.90 toolchain is missing: {cargo}")
    return cargo


def one_artifact(directory: Path, pattern: str) -> Path:
    matches = sorted(directory.glob(pattern))
    if len(matches) != 1:
        raise RuntimeError(f"expected one {pattern} under {directory}, got {matches}")
    return matches[0]


def target_cfg_names(target_rustc: Path) -> set[str]:
    environment = dict(os.environ)
    result = subprocess.run(
        [str(target_rustc), "-Z", "print-cfgs"],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError("target rustc failed to print its cfg set")
    entries = [line[1:] for line in result.stdout.splitlines() if line.startswith(">")]
    if not entries:
        raise RuntimeError("target rustc printed no cfg entries")
    return {entry.split("=", 1)[0] for entry in entries}


def rewrite_target_capability_cfg(text: str, enabled: set[str]) -> str:
    """Make exact rustc expand with the target backend's capability cfgs."""
    for name in sorted(TARGET_CAPABILITY_CFGS - enabled):
        text = re.sub(rf"\b{re.escape(name)}\b", f"trustme_disabled_{name}", text)
    return text


def copy_target_test_sources(
    library: Path,
    destination: Path,
    enabled_cfgs: set[str],
) -> None:
    roots = [
        (library / suite / "tests", destination / suite / "tests")
        for suite in SUITES
    ]
    roots.append(
        (library / "alloctests/testing", destination / "alloctests/testing")
    )
    for source, output_root in roots:
        for path in sorted(source.rglob("*")):
            if not path.is_file():
                continue
            output = output_root / path.relative_to(source)
            output.parent.mkdir(parents=True, exist_ok=True)
            if path.suffix == ".rs":
                text = path.read_text(encoding="utf-8", errors="surrogateescape")
                output.write_text(
                    rewrite_target_capability_cfg(text, enabled_cfgs),
                    encoding="utf-8",
                    errors="surrogateescape",
                )
            else:
                shutil.copyfile(path, output)


def cargo_harness_expansion(
    cargo: Path,
    manifest: Path,
    target: str,
    target_dir: Path,
    *,
    cwd: Path,
    environment: dict[str, str],
) -> str:
    return run_expansion(
        [
            str(cargo),
            "rustc",
            "--manifest-path", str(manifest),
            "--test", target,
            "--offline",
            "--target-dir", str(target_dir),
            "-j", str(os.cpu_count() or 1),
            "--",
            "-Zunpretty=expanded",
        ],
        cwd=cwd,
        environment=environment,
    )


def upstream_harness_locations(
    library: Path,
    rustc: Path,
    target_rustc: Path,
    std_roots: list[str],
) -> dict[str, set[tuple[str, int]]]:
    """Expand upstream harnesses with the target driver's backend cfg set."""
    source_root = library.parent
    cargo = exact_toolchain(source_root, rustc)
    with tempfile.TemporaryDirectory(prefix="trustme-rust-lib-import-") as temporary:
        work = Path(temporary)
        environment = dict(os.environ)
        environment["RUSTC"] = str(rustc)
        environment["RUSTC_BOOTSTRAP"] = "1"
        environment["CARGO_HOME"] = str(work / "cargo-home")

        alloc_target = work / "alloc-target"
        cargo_harness_expansion(
            cargo,
            library / "alloctests/Cargo.toml",
            "alloctests",
            alloc_target,
            cwd=source_root,
            environment=environment,
        )
        alloc_dependencies = alloc_target / "debug/deps"
        alloc_rand = one_artifact(alloc_dependencies, "librand-*.rlib")
        alloc_rand_xorshift = one_artifact(
            alloc_dependencies, "librand_xorshift-*.rlib"
        )
        cfg_if = work / "libcfg_if.rlib"
        # Compile this tiny macro crate once so std's env_modify harness can
        # expand cfg_if::cfg_if!.
        result = subprocess.run(
            [
                str(rustc),
                str(source_root / "vendor/cfg-if-1.0.1/src/lib.rs"),
                "--crate-name", "cfg_if",
                "--crate-type", "rlib",
                "--edition", "2018",
                "-o", str(cfg_if),
            ],
            cwd=source_root,
            env=environment,
            check=False,
        )
        if result.returncode != 0:
            raise RuntimeError("failed to compile cfg-if for std harness expansion")

        target_sources = work / "target-sources"
        enabled_cfgs = target_cfg_names(target_rustc)
        copy_target_test_sources(
            library,
            target_sources,
            enabled_cfgs,
        )

        def expand(
            suite: str,
            root: Path,
            edition: str,
            dependencies: Path,
            rand: Path,
            rand_xorshift: Path,
        ) -> tuple[str, set[tuple[str, int]]]:
            expanded = run_expansion(
                [
                    str(rustc),
                    str(root),
                    "--test",
                    "--edition", edition,
                    "-L", f"dependency={dependencies}",
                    "--extern", f"rand={rand}",
                    "--extern", f"rand_xorshift={rand_xorshift}",
                    "--extern", f"cfg_if={cfg_if}",
                    "-C", "debug-assertions=" + (
                        "yes" if "debug_assertions" in enabled_cfgs else "no"
                    ),
                    "-C", "overflow-checks=" + (
                        "yes" if "overflow_checks" in enabled_cfgs else "no"
                    ),
                    "-Z", "ub-checks=" + (
                        "yes" if "ub_checks" in enabled_cfgs else "no"
                    ),
                    "-Zunpretty=expanded",
                ],
                cwd=source_root,
                environment=environment,
            )
            return suite, expanded_test_locations(expanded, suite)

        harnesses = [
            (
                "coretests",
                target_sources / "coretests/tests/lib.rs",
                "2024",
                alloc_dependencies,
                alloc_rand,
                alloc_rand_xorshift,
            ),
            (
                "alloctests",
                target_sources / "alloctests/tests/lib.rs",
                "2021",
                alloc_dependencies,
                alloc_rand,
                alloc_rand_xorshift,
            ),
            (
                "alloctests",
                target_sources / "alloctests/tests/vec_deque_alloc_error.rs",
                "2021",
                alloc_dependencies,
                alloc_rand,
                alloc_rand_xorshift,
            ),
        ]
        harnesses.extend(
            (
                "std",
                target_sources / root,
                "2024",
                alloc_dependencies,
                alloc_rand,
                alloc_rand_xorshift,
            )
            for root in std_roots
        )
        locations = {suite: set() for suite in SUITES}
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as executor:
            futures = [executor.submit(expand, *harness) for harness in harnesses]
            for future in as_completed(futures):
                suite, harness_locations = future.result()
                locations[suite].update(harness_locations)
        return locations


NET_IP_SHARDS = {
    **dict.fromkeys(
        (
            "test_from_str_ipv4",
            "test_from_str_ipv6",
            "test_from_str_ipv4_in_ipv6",
            "test_from_str_socket_addr",
            "ipv4_addr_to_string",
            "ipv6_addr_to_string",
            "ipv4_to_ipv6",
            "ipv6_to_ipv4_mapped",
            "ipv6_to_ipv4",
        ),
        "net_ip_0",
    ),
    "ip_properties": "net_ip_properties",
    "ipv4_properties": "net_ipv4_properties",
    "ipv6_properties": "net_ipv6_properties",
    **dict.fromkeys(
        (
            "test_ipv4_to_int",
            "test_int_to_ipv4",
            "test_ipv6_to_int",
            "test_int_to_ipv6",
            "ipv4_from_constructors",
            "ipv6_from_constructors",
            "ipv4_from_octets",
            "ipv6_from_segments",
            "ipv6_from_octets",
        ),
        "net_ip_1",
    ),
    **dict.fromkeys(
        ("cmp", "is_v4", "is_v6", "ipv4_const", "ipv6_const", "ip_const", "structural_match"),
        "net_ip_2",
    ),
}


def apply_harness_layout(
    cases: list[tuple[str, str, str, str, str]],
    groups: dict[tuple[str, str], tuple[str, str, str]],
) -> tuple[
    list[tuple[str, str, str, str, str]],
    dict[tuple[str, str], tuple[str, str, str]],
]:
    """Apply the committed independent-harness layout to imported cases."""
    adapted = []
    for suite, group, source, function, hint in cases:
        if suite == "coretests" and group == "net":
            if source == "net/ip_addr.rs":
                group = NET_IP_SHARDS[function]
                hint = f"{group}::{function}"
            elif source == "net/parser.rs":
                group = "net_parser"
                hint = hint.replace("net::", "net_parser::", 1)
            elif source == "net/socket_addr.rs":
                group = "net_socket_addr"
                hint = hint.replace("net::", "net_socket_addr::", 1)
        adapted.append((suite, group, source, function, hint))

    groups = dict(groups)
    groups.pop(("coretests", "net"), None)
    for group in sorted(set(NET_IP_SHARDS.values())):
        groups[("coretests", group)] = (
            "module-shard",
            "coretests/tests/net/ip_addr.rs|coretests_net_ip_addr.rs",
            "2024",
        )
    groups[("coretests", "net_parser")] = (
        "adapter-module", "coretests_net_parser.rs", "2024"
    )
    groups[("coretests", "net_socket_addr")] = (
        "adapter-module", "coretests_net_socket_addr.rs", "2024"
    )
    groups[("std", "sync")] = ("adapter-crate", "std_sync.rs", "2024")
    return adapted, groups


def select_target_applicable_cases(
    case_records: list[tuple[tuple[str, str, str, str, str], list[int]]],
    locations: dict[str, set[tuple[str, int]]],
) -> tuple[
    list[tuple[str, str, str, str, str]],
    list[tuple[str, str, str, str, str, str, str]],
]:
    cases = []
    excluded = []
    for case, function_lines in case_records:
        suite, _, source_relative, _, _ = case
        source = f"{suite}/tests/{source_relative}"
        if any((source, line) in locations[suite] for line in function_lines):
            cases.append(case)
        else:
            lines = ",".join(str(line) for line in function_lines)
            excluded.append((*case, lines, "absent-from-target-harness"))
    return cases, excluded


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--rustc", type=Path, required=True)
    parser.add_argument("--target-rustc", type=Path, required=True)
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    rustc = arguments.rustc.resolve()
    target_rustc = arguments.target_rustc.resolve()
    library = source / "library" if (source / "library").is_dir() else source
    suites = {
        "coretests": library / "coretests" / "tests",
        "alloctests": library / "alloctests" / "tests",
        "std": library / "std" / "tests",
    }
    if not all(path.is_dir() for path in suites.values()):
        raise SystemExit(f"missing Rust library test suites under {library}")

    if UPSTREAM.exists():
        shutil.rmtree(UPSTREAM)
    for suite, tests in suites.items():
        copy_tree(tests, UPSTREAM / suite / "tests")
    copy_tree(library / "alloctests" / "testing", UPSTREAM / "alloctests" / "testing")

    (UPSTREAM / "coretests" / "preamble.rs").write_text(
        preamble(suites["coretests"] / "lib.rs")
    )
    (UPSTREAM / "alloctests" / "preamble.rs").write_text(
        preamble(suites["alloctests"] / "lib.rs")
    )

    case_lines = {}
    groups = {}
    for suite, tests in suites.items():
        for path in sorted(tests.rglob("*.rs")):
            functions = test_function_items(path.read_text(errors="surrogateescape"))
            if not functions:
                continue
            if suite == "std":
                group, root, module_hint = std_group(tests, path)
                kind, edition = "crate", "2024"
                source_relative = path.relative_to(tests).as_posix()
                root_relative = f"std/tests/{root}"
            else:
                group, root, module_hint = module_group(tests, path)
                kind = "module"
                edition = "2024" if suite == "coretests" else "2021"
                source_relative = path.relative_to(tests).as_posix()
                root_relative = "-" if root == "-" else f"{suite}/{root}"
            groups[(suite, group)] = (kind, root_relative, edition)
            for name, inline_modules, _, function_line in functions:
                hint = "::".join(
                    part for part in (module_hint, *inline_modules, name) if part
                )
                case = (suite, group, source_relative, name, hint)
                case_lines.setdefault(case, []).append(function_line)

    case_records = list(case_lines.items())

    std_roots = sorted(
        root
        for (suite, _), (_, root, _) in groups.items()
        if suite == "std"
    )
    locations = upstream_harness_locations(library, rustc, target_rustc, std_roots)
    cases, excluded = select_target_applicable_cases(case_records, locations)

    cases, groups = apply_harness_layout(cases, groups)
    used_groups = {(suite, group) for suite, group, _, _, _ in cases}
    groups = {key: value for key, value in groups.items() if key in used_groups}

    (HERE / "groups.tsv").write_text(
        "".join(
            f"{suite}\t{group}\t{kind}\t{root}\t{edition}\n"
            for (suite, group), (kind, root, edition) in sorted(groups.items())
        )
    )
    (HERE / "cases.tsv").write_text(
        "".join("\t".join(case) + "\n" for case in cases)
    )
    (HERE / "excluded_cases.tsv").write_text(
        "".join("\t".join(case) + "\n" for case in excluded)
    )
    print(
        f"imported {len(cases)} target-applicable explicit library tests "
        f"in {len(groups)} harness groups; recorded {len(excluded)} "
        "target-excluded source tests"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
