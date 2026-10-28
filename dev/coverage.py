#!/usr/bin/env python3
"""Merge per-unit LLVM profiles and report production rustc coverage."""

import argparse
from concurrent.futures import ThreadPoolExecutor
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


def line_counts(record):
    counts = {}
    for line in record.splitlines():
        if line.startswith("DA:"):
            number, hits, *_ = line[3:].split(",")
            counts[int(number)] = counts.get(int(number), 0) + int(hits)
    return sum(hits > 0 for hits in counts.values()), len(counts)


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
        def merge_group(start):
            target = work / f"{round_index}-{start // 8}.profdata"
            subprocess.run([
                "llvm-profdata", "merge", "-sparse",
                *map(str, generation[start:start + 8]),
                "-o", str(target),
            ], cwd=root, check=True)
            return target
        # Limit both fan-in and concurrent LLVM processes: the corpus now
        # includes thousands of CLI compilations, each with a full profile.
        with ThreadPoolExecutor(max_workers=4) as workers:
            merged = list(workers.map(merge_group, range(0, len(generation), 8)))
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
                # LLVM's summary can count lines absent from its DA records,
                # especially in macro expansions. Codecov consumes DA: keep
                # the exported totals and our local gate on that same basis.
                hit, total = line_counts("\n".join(lines))
                lines = [line for line in lines if not line.startswith(("LF:", "LH:"))]
                lines.extend((f"LF:{total}", f"LH:{hit}"))
                records.append("\n".join(lines) + "\nend_of_record\n")
            break
    if not records:
        raise RuntimeError("coverage export contains no production rustc sources")
    output.write_text("".join(records))
    return records


def check_floor(records, minimum):
    counts = [line_counts(record) for record in records]
    found = sum(hit for hit, _ in counts)
    total = sum(lines for _, lines in counts)
    coverage = 100.0 * found / total if total else 0.0
    print(f"line coverage: {found}/{total} = {coverage:.2f}%")
    if not total:
        raise RuntimeError("coverage export contains no instrumented lines")
    if coverage < minimum:
        raise RuntimeError(f"line coverage {coverage:.2f}% is below {minimum:.2f}%")


def union_lines(records):
    """Count each source line once across independently exported variants."""
    sources = {}
    for record in records:
        lines = record.splitlines()
        name = next(line[3:] for line in lines if line.startswith("SF:"))
        counts = sources.setdefault(name, {})
        for line in lines:
            if line.startswith("DA:"):
                number, hits, *_ = line[3:].split(",")
                number = int(number)
                counts[number] = counts.get(number, 0) + int(hits)
    result = []
    for name, counts in sorted(sources.items()):
        lines = [f"SF:{name}"]
        lines.extend(f"DA:{number},{hits}" for number, hits in sorted(counts.items()))
        lines.extend((f"LF:{len(counts)}", f"LH:{sum(hits > 0 for hits in counts.values())}"))
        result.append("\n".join(lines) + "\nend_of_record\n")
    return result


def write_summary(records, output):
    rows = []
    for record in records:
        name = next(line[3:] for line in record.splitlines() if line.startswith("SF:"))
        hit, total = line_counts(record)
        rows.append((name, hit, total))
    width = max(len(name) for name, _, _ in rows)
    with output.open("w") as stream:
        stream.write(f"{'Source':<{width}} {'Lines':>8} {'Hit':>8} {'Missed':>8} {'Coverage':>9}\n")
        for name, hit, total in sorted(rows):
            percent = f"{100 * hit / total:.2f}%" if total else "-"
            stream.write(f"{name:<{width}} {total:>8} {hit:>8} {total - hit:>8} {percent:>9}\n")
        hit = sum(row[1] for row in rows)
        total = sum(row[2] for row in rows)
        percent = f"{100 * hit / total:.2f}%" if total else "-"
        stream.write(f"{'TOTAL':<{width}} {total:>8} {hit:>8} {total - hit:>8} {percent:>9}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    export = parser.add_mutually_exclusive_group(required=True)
    export.add_argument("--rustc", type=Path)
    export.add_argument("--reports", nargs="+", type=Path,
                        help="union line coverage from independently exported compiler variants")
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

    if args.reports:
        records = union_lines([
            record.strip() for path in args.reports
            for record in path.read_text().split("end_of_record") if record.strip()
        ])
        (args.output / "coverage.info").write_text("".join(records))
    else:
        objects = [str(args.rustc)]
        for path in args.objects:
            objects.extend(["-object", str(path)])
        relative_sources = [path.relative_to(root).as_posix() for path in sources]
        options = [*objects, f"-instr-profile={profile}", *relative_sources]
        records = export_lcov(root, sources, options, args.output / "coverage.info")

    print("coverage: writing summaries", flush=True)
    summary_path = args.output / "summary.txt"
    write_summary(records, summary_path)

    print(summary_path.read_text(), end="")
    check_floor(records, args.minimum)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"coverage: {error}", file=sys.stderr)
        sys.exit(1)
