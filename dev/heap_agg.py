#!/usr/bin/env python3
"""Aggregate a gperftools heap dump by source line of the compiler.

    heap_agg.py DUMP.heap RUSTC_BINARY [TOP]

Parses the text dump written by HEAPPROFILE (see dev/MEMPROF.md), batch
symbolises every unique return address with llvm-symbolizer, attributes
each allocation record to the first stack frame inside bin/rustc (skipping
container infrastructure like ThinVector), and prints the top lines by
cumulative allocated bytes and by allocation count.

Set LLVM_SYMBOLIZER to override the symboliser binary (defaults to
`llvm-symbolizer` from PATH).
"""

import os
import re
import subprocess
import sys

DEPTH = 12  # stack frames to consider per record
INFRA = ("thin_vector.h", "rc_string.h", "range_vec_map.h")


def parse_dump(path):
    rec = re.compile(
        r"^\s*\d+:\s+\d+\s+\[\s*(\d+):\s+(\d+)\]\s+@((?:\s+0x[0-9a-f]+)+)")
    records = []
    addrs = set()
    with open(path, errors="replace") as fh:
        for line in fh:
            m = rec.match(line)
            if not m:
                if line.startswith("MAPPED") or line.startswith("build="):
                    break
                continue
            objs, size = int(m.group(1)), int(m.group(2))
            if objs == 0 and size == 0:
                continue
            stack = m.group(3).split()[:DEPTH]
            records.append((objs, size, stack))
            addrs.update(stack)
    return records, addrs


def symbolise(binary, addrs):
    # The default (LLVM) output style separates addresses with blank lines;
    # the GNU style does not, which breaks batch parsing.
    symbolizer = os.environ.get("LLVM_SYMBOLIZER", "llvm-symbolizer")
    out = subprocess.run(
        [symbolizer, "-e", binary, "--functions=short"],
        input="\n".join(addrs), capture_output=True, text=True,
        check=True).stdout
    sym = {}
    for addr, block in zip(addrs, out.strip().split("\n\n")):
        lines = block.strip().splitlines()
        sym[addr] = [(lines[i], lines[i + 1])
                     for i in range(0, len(lines) - 1, 2)]
    return sym


def our_frame(sym, stack, skip_infra):
    for addr in stack:
        for func, loc in sym.get(addr, []):
            if "bin/rustc/" in loc:
                short = loc.split("bin/rustc/")[-1]
                if skip_infra and short.startswith(INFRA):
                    continue
                return func, short
    return None


def aggregate(sym, records, skip_infra):
    table = {}
    other = [0, 0]
    for objs, size, stack in records:
        hit = our_frame(sym, stack, skip_infra)
        if hit is None:
            other[0] += size
            other[1] += objs
            continue
        cell = table.setdefault(hit, [0, 0])
        cell[0] += size
        cell[1] += objs
    return table, other


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    dump, binary = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) == 4 else 30

    records, addrs = parse_dump(dump)
    print(f"records={len(records)} unique_addrs={len(addrs)}",
          file=sys.stderr)
    sym = symbolise(binary, addrs)

    table, (other_b, other_o) = aggregate(sym, records, skip_infra=True)
    tot_b = sum(v[0] for v in table.values()) + other_b
    tot_o = sum(v[1] for v in table.values()) + other_o
    print(f"TOTAL alloc: {tot_b / 1e9:.2f} GB / {tot_o / 1e6:.1f}M objects;"
          f" outside bin/rustc: {other_b / 1e9:.2f} GB /"
          f" {other_o / 1e6:.1f}M")

    print("\n=== TOP BY BYTES ===")
    for (func, loc), (b, o) in sorted(table.items(),
                                      key=lambda kv: -kv[1][0])[:top]:
        print(f"{b / 1e6:10.1f} MB {o / 1e6:8.2f}M  {loc:55s} {func[:60]}")
    print("\n=== TOP BY COUNT ===")
    for (func, loc), (b, o) in sorted(table.items(),
                                      key=lambda kv: -kv[1][1])[:top]:
        print(f"{o / 1e6:8.2f}M {b / 1e6:10.1f} MB  {loc:55s} {func[:60]}")

    raw, _ = aggregate(sym, records, skip_infra=False)
    print("\n=== INFRA VIEW (first bin/rustc frame, infrastructure kept) ===")
    for (func, loc), (b, o) in sorted(raw.items(),
                                      key=lambda kv: -kv[1][0])[:10]:
        print(f"{b / 1e6:10.1f} MB {o / 1e6:8.2f}M  {loc:55s} {func[:60]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
