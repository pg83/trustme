#!/usr/bin/env python3
"""Generate a fixed RustSmith corpus and record rustc 1.90 stdout oracles."""

import argparse
import concurrent.futures
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def run_case(generator: Path, rustc: Path, seed: int) -> tuple[int, bytes, bytes, bytes]:
    with tempfile.TemporaryDirectory(prefix=f"rustsmith-{seed:04d}-") as work_name:
        work = Path(work_name)
        generated = work / "generated"
        generate = subprocess.run(
            [
                str(generator),
                "-n", "1",
                "-t", "1",
                "-s", str(seed),
                "--directory", str(generated),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=180,
            check=False,
        )
        if generate.returncode != 0:
            raise RuntimeError(
                f"RustSmith failed for seed {seed}:\n"
                + generate.stderr.decode(errors="replace")
            )

        source = (generated / "file0" / "file0.rs").read_bytes()
        arguments = (generated / "file0" / "file0.txt").read_bytes()
        source_path = work / "case.rs"
        binary = work / "reference"
        source_path.write_bytes(source)
        compile_result = subprocess.run(
            [
                str(rustc),
                "--edition", "2021",
                "-O",
                "-C", "linker=/usr/bin/gcc",
                str(source_path),
                "-o", str(binary),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=180,
            check=False,
        )
        if compile_result.returncode != 0:
            raise RuntimeError(
                f"rustc 1.90 failed for seed {seed}:\n"
                + compile_result.stderr.decode(errors="replace")
            )

        try:
            run_result = subprocess.run(
                [str(binary), *shlex.split(arguments.decode())],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=10,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"reference program timed out for seed {seed}") from error
        if run_result.returncode != 0:
            raise RuntimeError(
                f"reference program failed for seed {seed}:\n"
                + run_result.stderr.decode(errors="replace")
            )
        return seed, source, arguments, run_result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    parser.add_argument("rustc", type=Path)
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--candidates", type=int)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 1))
    args = parser.parse_args()

    generator = (args.checkout / "run" / "rustsmith").resolve()
    rustc = args.rustc.resolve()
    if not generator.is_file():
        parser.error(f"missing built generator: {generator}")
    if not rustc.is_file():
        parser.error(f"missing reference compiler: {rustc}")

    candidate_count = args.candidates or args.count + 32
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(run_case, generator, rustc, seed): seed
            for seed in range(candidate_count)
        }
        results = []
        for future in concurrent.futures.as_completed(futures):
            seed = futures[future]
            try:
                result = future.result()
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                print(f"skipped seed {seed}: {error}", flush=True)
                continue
            results.append(result)
            print(f"generated seed {result[0]}", flush=True)

    results.sort()
    if len(results) < args.count:
        raise SystemExit(
            f"only {len(results)} of {args.count} requested cases succeeded "
            f"among {candidate_count} candidate seeds"
        )
    results = results[:args.count]

    upstream = ROOT / "upstream"
    if upstream.exists():
        shutil.rmtree(upstream)
    upstream.mkdir()
    manifest = []
    for seed, source, arguments, stdout in results:
        stem = f"{seed:04d}"
        (upstream / f"{stem}.rs").write_bytes(source)
        (upstream / f"{stem}.args").write_bytes(arguments)
        (upstream / f"{stem}.stdout").write_bytes(stdout)
        manifest.append(f"{stem}\t{seed}\n")
    (ROOT / "cases.tsv").write_text("".join(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
