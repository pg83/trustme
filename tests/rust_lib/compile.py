#!/usr/bin/env python3
"""Compile one grouped Rust 1.90 library-test harness."""

import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 9:
        raise SystemExit(
            "usage: compile.py SUITE GROUP KIND ROOT EDITION UPSTREAM LIBSTD_TAR OUTPUT"
        )
    suite, group, kind, root, edition, upstream, libstd_tar, output = sys.argv[1:]
    upstream = os.path.abspath(upstream)
    libstd_tar = os.path.abspath(libstd_tar)
    output = os.path.abspath(output)
    rustc = lib.require_env("RUSTC")
    environment = dict(os.environ)
    environment["MRUSTC_TARGET_VER"] = "1.90"
    environment.setdefault("CC", "cc")

    print(f"[Rust library harness] {suite}/{group}", file=sys.stderr, flush=True)
    with lib.workdir() as work:
        libstd = lib.untar(libstd_tar, os.path.join(work, "libstd"))
        if kind == "crate":
            source = os.path.join(upstream, root)
        elif kind == "adapter-crate":
            source = os.path.join(os.path.dirname(__file__), "adapter", root)
        elif kind in ("module", "adapter-module", "module-shard", "module-support"):
            preamble = os.path.join(upstream, suite, "preamble.rs")
            wrapper = open(preamble, encoding="utf-8", errors="surrogateescape").read()
            if suite == "alloctests":
                support = os.path.join(upstream, suite, "tests", "testing", "mod.rs")
                support = os.path.relpath(support, work)
                wrapper += f"\n#[path = {json.dumps(support)}]\nmod testing;\n"
            if root != "-":
                if kind in ("module-shard", "module-support"):
                    root, support = root.split("|", 1)
                    support = os.path.join(os.path.dirname(__file__), "adapter", support)
                    wrapper += "\n" + open(
                        support, encoding="utf-8", errors="surrogateescape"
                    ).read()
                if kind == "module-shard":
                    selected = {
                        line.split("\t")[3]
                        for line in open(
                            os.path.join(os.path.dirname(__file__), "cases.tsv"),
                            encoding="utf-8",
                        )
                        if line.split("\t", 2)[:2] == [suite, group]
                    }
                    module_source = open(
                        os.path.join(upstream, root),
                        encoding="utf-8",
                        errors="surrogateescape",
                    ).read()
                    found = set(re.findall(r"(?m)^#\[test\]\nfn ([A-Za-z0-9_]+)", module_source))
                    missing = selected - found
                    if missing:
                        raise SystemExit(
                            f"{suite}/{group}: selected tests not found: {sorted(missing)}"
                        )
                    module_source = re.sub(
                        r"(?m)^#\[test\]\nfn ([A-Za-z0-9_]+)",
                        lambda match: (
                            match.group(0)
                            if match.group(1) in selected
                            else f"#[test]\n#[cfg(any())]\nfn {match.group(1)}"
                        ),
                        module_source,
                    )
                    module = os.path.join(work, f"{group}.rs")
                    open(module, "w", encoding="utf-8").write(module_source)
                elif kind == "adapter-module":
                    module = os.path.join(os.path.dirname(__file__), "adapter", root)
                else:
                    module = os.path.join(upstream, root)
                module = os.path.relpath(module, work)
                wrapper += f"\n#[path = {json.dumps(module)}]\nmod {group};\n"
            source = os.path.join(work, "wrapper.rs")
            open(source, "w", encoding="utf-8").write(wrapper)
        else:
            raise SystemExit(f"unknown harness kind: {kind}")

        os.makedirs(os.path.dirname(output), exist_ok=True)
        result = subprocess.run(
            [
                rustc,
                source,
                "-L", os.path.join(libstd, "release"),
                "-o", output,
                "--test",
                "--edition", edition,
                "--crate-name",
                re.sub(r"[^A-Za-z0-9_]", "_", f"rust_lib_{suite}_{group}"),
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


if __name__ == "__main__":
    raise SystemExit(main())
