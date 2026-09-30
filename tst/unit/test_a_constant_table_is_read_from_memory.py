#!/usr/bin/env python3
"""A `const` whose value is an aggregate is an allocation upstream
(`ConstValue::Indirect`): codegen copies it out of a global, which the
optimizer turns into a load from the global. streebog indexes
`SHUFFLED_LIN_TABLE: [[u64; 256]; 8]` in its inner loop, and the constant
was rebuilt element by element at every lookup - 2048 stores per load, and
pbkdf2's streebog test ran for three minutes. The generated C must not
rebuild the table."""

import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import lib  # noqa: E402

SOURCE = """
pub const TABLE: [[u64; 256]; 2] = {
    let mut table = [[0u64; 256]; 2];
    let mut i = 0;
    while i < 256 {
        table[0][i] = i as u64 * 3 + 1;
        table[1][i] = i as u64 * 7 + 1;
        i += 1;
    }
    table
};

#[inline(never)]
pub fn look(j: usize, i: usize) -> u64 {
    TABLE[j][i]
}
"""


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_a_constant_table_is_read_from_memory.py RUSTC LIBSTD_TAR STAMP")
    rustc, libstd_tar, stamp_text = sys.argv[1:]

    with lib.workdir() as work_text:
        work = Path(work_text)
        libstd = Path(lib.untar(libstd_tar, str(work / "libstd")))
        source = work / "table.rs"
        source.write_text(SOURCE)
        output = work / "libtable.rlib"
        subprocess.run(
            lib.wrap_gdb([
                rustc, str(source), "--crate-type", "lib", "--edition", "2021", "-O",
                "-L", str(libstd / "release"), "-C", "emit-cpp-only", "-o", str(output),
            ]),
            check=True,
        )
        generated = Path(str(output) + ".cpp").read_text(errors="replace")
        if ".DATA[255] = " in generated:
            raise SystemExit("the constant table is rebuilt element by element where it is read")

    stamp = Path(stamp_text)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
