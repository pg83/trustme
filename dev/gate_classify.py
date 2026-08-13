#!/usr/bin/env python3
"""Classify independently rerun gate failures by their first stable signature."""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re


ERROR = re.compile(r" error:[A-Z0-9]*: (.*)")
GENERIC_ABORT_FRAMES = {"span.cpp:75", "span.cpp:82", "mir_helpers.cpp:29"}
ROOT = Path(__file__).resolve().parent.parent
SOURCE_LOCATION = re.compile(re.escape(str(ROOT / "bin" / "rustc")) + r"/([^: ]+:[0-9]+)")
FRAME_LOCATION = re.compile(r" at " + re.escape(str(ROOT / "bin" / "rustc")) + r"/([^: ]+:[0-9]+)")


def first_line(text: str, marker: str) -> str | None:
    return next((line for line in text.splitlines() if marker in line), None)


def caller(text: str) -> str:
    thread = text.find("Thread 1 (process")
    if thread < 0:
        return "no-backtrace"
    for match in FRAME_LOCATION.finditer(text, thread):
        location = match.group(1)
        if location not in GENERIC_ABORT_FRAMES:
            return location
    return "no-rustc-frame"


def compiler_location(line: str | None) -> str | None:
    if line is None:
        return None
    match = SOURCE_LOCATION.search(line)
    return match.group(1) if match else None


def case_name(text: str, target: str) -> str:
    failures = [
        line.removeprefix("FAIL ")
        for line in text.splitlines()
        if line.startswith("FAIL ") and "$(B)" not in line
    ]
    if failures:
        return failures[-1]
    headings = [
        line[1 : line.index("]")]
        for line in text.splitlines()
        if line.startswith("[") and "]" in line
    ]
    return headings[0] if headings else target


def diagnostic(text: str) -> str | None:
    for line in text.splitlines():
        match = ERROR.search(line)
        if match:
            return match.group(1)
    return None


def child_exit(text: str) -> int | None:
    matches = re.findall(r"(?:compile |runtime )?exit (-?[0-9]+)", text)
    return int(matches[-1]) if matches else None


def abort_signature(text: str) -> tuple[str, str]:
    frame = caller(text)
    line = first_line(text, "MIR TODO:")
    if line:
        location = compiler_location(line) or frame
        return "mir-todo", f"MIR TODO {location}"
    line = first_line(text, "MIR ERROR:")
    if line:
        location = compiler_location(line) or frame
        return "mir-bug", f"MIR ERROR {location}"
    line = first_line(text, "BUG:")
    if line:
        location = compiler_location(line) or frame
        return "bug", f"BUG {location}"
    message = diagnostic(text)
    if message is not None:
        return "compile-error", f"ERROR {frame}"
    assertion = first_line(text, "Assertion `")
    if assertion:
        return "assert", f"ASSERT {compiler_location(assertion) or frame}"
    exception = re.search(r"uncaught exception of type ([^\n]+)", text)
    if exception:
        return "exception", f"EXCEPTION {exception.group(1)} {frame}"
    return "abort", f"SIGABRT {frame}"


def classify(result: dict[str, object], text: str) -> tuple[str, str]:
    code = result["rerun_code"]
    frame = caller(text)
    child = child_exit(text)
    if code == 0:
        return "flake", "passes-in-isolation"
    if (child == -6 and (
        "runtime exit -6" in text or re.search(r"FAIL .*: exit -6, mode ", text)
    )) or ("\nrunning 1 test\n" in text and "memory allocation" in text):
        return "runtime", "runtime-SIGABRT"
    if code == 124:
        return "timeout", "stable-timeout"
    if code == 245:
        return "signal", f"SIGSEGV {frame}"
    if code == 252:
        return "signal", f"SIGILL {frame}"
    if code == 101:
        return "runtime", "exit-101"
    if code == 1:
        if "unexpected compile success" in text:
            return "diagnostic", "unexpected-compile-success"
        if child == -6:
            return abort_signature(text)
        if child == 101:
            return "runtime", "exit-101"
        if "C Compiler failed to execute" in text:
            if "cannot find -lrust_test_helpers" in text:
                return "harness", "missing-rust-test-helpers"
            if "unknown token in expression" in text:
                return "codegen", "invalid-inline-assembly"
            if "linker command failed" in text:
                return "codegen", "link-failure"
            return "codegen", "generated-cpp-rejected"
        if "stdout differs" in text:
            return "runtime", "stdout-mismatch"
        if child == 1:
            return "reject", "compile-exit-1"
        return "adapter", "adapter-exit-1"
    if code != 250:
        return "other", f"exit-{code}"
    return abort_signature(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    records = []
    for line in args.results.read_text().splitlines():
        result = json.loads(line)
        text = Path(result["log"]).read_text(errors="replace")
        kind, signature = classify(result, text)
        records.append(
            {
                "case": case_name(text, result["target"]),
                "diagnostic": diagnostic(text),
                "gate_code": result["gate_code"],
                "kind": kind,
                "log": result["log"],
                "rerun_code": result["rerun_code"],
                "signature": signature,
                "target": result["target"],
            }
        )

    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / "records.jsonl").open("w") as stream:
        for record in records:
            stream.write(json.dumps(record, sort_keys=True) + "\n")

    clusters: dict[str, list[dict[str, object]]] = defaultdict(list)
    for record in records:
        clusters[record["signature"]].append(record)
    cluster_rows = []
    for signature, entries in clusters.items():
        cluster_rows.append(
            {
                "count": len(entries),
                "diagnostics": Counter(
                    entry["diagnostic"] for entry in entries if entry["diagnostic"]
                ).most_common(10),
                "examples": [entry["case"] for entry in entries[:10]],
                "kind": entries[0]["kind"],
                "signature": signature,
                "targets": [entry["target"] for entry in entries],
            }
        )
    cluster_rows.sort(key=lambda row: (-row["count"], row["signature"]))
    (args.output / "clusters.json").write_text(json.dumps(cluster_rows, indent=2))

    for row in cluster_rows:
        print(f"{row['count']:4}  {row['signature']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
