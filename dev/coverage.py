#!/usr/bin/env python3
"""Merge per-unit LLVM profiles and report production rustc coverage."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def source_files(root):
    result = []
    for pattern in ("*.cpp", "*.h", "*.inc"):
        for path in (root / "bin" / "rustc").glob(pattern):
            if path.name.endswith("_ut.cpp") or path.name == "tagged_union_sample.cpp":
                continue
            result.append(path.resolve())
    return sorted(result)


def run_output(command, cwd):
    return subprocess.check_output(command, cwd=cwd, text=True)


def run_to_file(command, cwd, output):
    with output.open("w") as stream:
        subprocess.run(command, cwd=cwd, stdout=stream, check=True)


def merge_profiles(root, profiles, output):
    # A single invocation with the whole unit corpus has a large and useless
    # memory spike. Merge bounded groups until one profile remains.
    work = output.parent / "merge-work"
    work.mkdir(exist_ok=True)
    generation = profiles
    round_index = 0
    print(f"coverage: merging {len(profiles)} profiles", flush=True)
    while len(generation) > 1:
        print(f"coverage: merge round {round_index}, {len(generation)} inputs", flush=True)
        merged = []
        for start in range(0, len(generation), 8):
            target = work / f"{round_index}-{start // 8}.profdata"
            subprocess.run([
                "llvm-profdata", "merge", "-sparse",
                *map(str, generation[start:start + 8]),
                "-o", str(target),
            ], cwd=root, check=True)
            merged.append(target)
        generation = merged
        round_index += 1
    shutil.copyfile(generation[0], output)


def export_lcov(root, sources, options, output):
    print("coverage: exporting LCOV", flush=True)
    trace = run_output(["llvm-cov", "export", "-format=lcov", *options], root)
    wanted = set(sources)
    records = []
    for record in trace.split("end_of_record"):
        lines = record.strip().splitlines()
        for index, line in enumerate(lines):
            if not line.startswith("SF:"):
                continue
            path = Path(line[3:])
            if not path.is_absolute():
                path = root / path
            path = path.resolve()
            if path in wanted:
                lines[index] = "SF:" + path.relative_to(root).as_posix()
                records.append("\n".join(lines) + "\nend_of_record\n")
            break
    if not records:
        raise RuntimeError("coverage export contains no production rustc sources")
    output.write_text("".join(records))
    return records


def check_floor(records, minimum):
    found = 0
    total = 0
    for line in "".join(records).splitlines():
        if line.startswith("LF:"):
            total += int(line[3:])
        elif line.startswith("LH:"):
            found += int(line[3:])
    coverage = 100.0 * found / total if total else 0.0
    print(f"line coverage: {found}/{total} = {coverage:.2f}%")
    if not total:
        raise RuntimeError("coverage export contains no instrumented lines")
    if coverage < minimum:
        raise RuntimeError(f"line coverage {coverage:.2f}% is below {minimum:.2f}%")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rustc", required=True, type=Path)
    parser.add_argument("--objects", nargs="*", default=[], type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--minimum", type=float, default=0.0)
    parser.add_argument("profiles", nargs="+", type=Path)
    args = parser.parse_args()

    root = args.source_root.resolve()
    sources = source_files(root)
    if not sources:
        raise RuntimeError("no production rustc sources found")
    profiles = []
    for path in args.profiles:
        if path.is_dir():
            profiles.extend(path.glob("*.profraw"))
        elif path.is_file() and path.stat().st_size:
            profiles.append(path)
    profiles = sorted(profiles)
    if not profiles:
        raise RuntimeError("unit tests produced no coverage profiles")

    args.output.mkdir(parents=True, exist_ok=True)
    profile = args.output / "coverage.profdata"
    merge_profiles(root, profiles, profile)

    objects = [str(args.rustc)]
    for path in args.objects:
        objects.extend(["-object", str(path)])
    relative_sources = [path.relative_to(root).as_posix() for path in sources]
    options = [*objects, f"-instr-profile={profile}", *relative_sources]
    records = export_lcov(root, sources, options, args.output / "coverage.info")

    print("coverage: writing summaries", flush=True)
    summary_path = args.output / "summary.txt"
    run_to_file(["llvm-cov", "report", *options], root, summary_path)
    run_to_file(["llvm-cov", "report", "-show-functions", *options], root, args.output / "functions.txt")
    print("coverage: writing line report", flush=True)
    run_to_file(["llvm-cov", "show", "-show-branches=count", *options], root, args.output / "show.txt")

    print(summary_path.read_text(), end="")
    check_floor(records, args.minimum)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"coverage: {error}", file=sys.stderr)
        sys.exit(1)
