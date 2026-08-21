#!/usr/bin/env python3
"""Rerun failed gate nodes independently and retain one log per node."""

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


FAILURE = re.compile(
    r"FAIL \$\(B\)/tst/(?P<target>\S+): command exited (?P<code>\d+): (?P<command>.*)$"
)


def load_failures(path: Path) -> list[dict[str, object]]:
    failures = []
    for line in path.read_text(errors="replace").splitlines():
        match = FAILURE.search(line)
        if not match:
            continue
        failures.append(
            {
                "target": match.group("target"),
                "gate_code": int(match.group("code")),
                "command": shlex.split(match.group("command")),
            }
        )
    return failures


def rewrite_command(command: list[str], root: Path, out: Path, index: int) -> list[str]:
    temp_root = re.compile(re.escape(str(root / ".build-clang" / "tmp")) + r"/[0-9a-f]+")
    rewritten = []
    for arg in command:
        arg = temp_root.sub(str(root / ".build-clang"), arg)
        if arg.endswith(".stamp") and str(root / ".build-clang" / "tst") in arg:
            arg = str(out / "stamps" / f"{index:04d}.stamp")
        rewritten.append(arg)
    return rewritten


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("gate_log", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    (output / "logs").mkdir(parents=True, exist_ok=True)
    (output / "stamps").mkdir(parents=True, exist_ok=True)
    failures = load_failures(args.gate_log)
    if not failures:
        raise SystemExit("no failed gate commands found")

    environment = dict(os.environ)
    environment.update(
        {
            "RUSTC": str(root / ".build-clang" / "bin" / "rustc"),
            "CARGO": str(root / ".build-clang" / "bin" / "cargo"),
            "CC": os.environ.get("CC", "clang"),
            "CXX": os.environ.get("CXX", "clang++"),
            "LDFLAGS": os.environ.get("LDFLAGS", "-fuse-ld=lld"),
        }
    )

    def run(entry: tuple[int, dict[str, object]]) -> dict[str, object]:
        index, failure = entry
        command = rewrite_command(failure["command"], root, output, index)
        result = subprocess.run(
            command,
            cwd=root,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        log_path = output / "logs" / f"{index:04d}.log"
        log_path.write_bytes(result.stdout)
        return {
            "index": index,
            "target": failure["target"],
            "gate_code": failure["gate_code"],
            "rerun_code": result.returncode,
            "command": command,
            "log": str(log_path),
        }

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for result in executor.map(run, enumerate(failures)):
            results.append(result)

    results.sort(key=lambda result: result["index"])
    summary = output / "results.jsonl"
    with summary.open("w") as stream:
        for result in results:
            stream.write(json.dumps(result, sort_keys=True) + "\n")
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
