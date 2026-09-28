#!/usr/bin/env python3
"""rustc's `--emit` names the outputs a compilation writes. autocfg probes with
`--crate-type=lib --out-dir DIR --emit=llvm-ir` and removes `DIR/<crate>.ll`
alone; its tests check the directory is then empty. `llvm-ir` without `link`
writes the backend's IR (for us the generated C++) and nothing else;
`dep-info` names `<crate>.d` beside the others."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_emit_llvm_ir.py RUSTC LIBSTD_TAR STAMP")
    rustc, libstd_tar, stamp_text = sys.argv[1:]
    environment = dict(os.environ)
    environment.setdefault("CC", "cc")

    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(libstd_tar, str(work / "libstd")))
        probe = work / "probe"
        probe.mkdir()
        subprocess.run(
            [rustc, "--crate-name", "autocfg_probe_1", "--crate-type=lib", "--out-dir", str(probe),
             "--emit=llvm-ir", "-L", str(libstd / "release"), "-"],
            input="pub type Probe = i32;\npub fn probe() -> Probe { 1 }\n",
            text=True, env=environment, check=True,
        )
        written = sorted(path.name for path in probe.iterdir())
        if written != ["autocfg_probe_1.ll"]:
            raise SystemExit(f"--emit=llvm-ir wrote {written}")

        both = work / "both"
        both.mkdir()
        subprocess.run(
            [rustc, "--crate-name", "deps_probe", "--crate-type=lib", "--out-dir", str(both),
             "--emit=dep-info,link", "-L", str(libstd / "release"), "-"],
            input="pub fn probe() -> i32 { 2 }\n",
            text=True, env=environment, check=True,
        )
        written = sorted(path.name for path in both.iterdir())
        for wanted in ("deps_probe.d", "libdeps_probe.rlib"):
            if wanted not in written:
                raise SystemExit(f"--emit=dep-info,link wrote {written}, not {wanted}")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
