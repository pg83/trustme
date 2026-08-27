#!/usr/bin/env python3
"""Fail while compiler object files contain static or thread-local objects."""

import collections
import re
import subprocess
import sys


STORAGE_TYPES = frozenset("bBcCdDgGrRsSuVv")

# nm also exposes data emitted by the C++ ABI and the compiler itself.  None of
# these names denotes an object declared by the trustme sources.
IGNORED_PREFIXES = (".", "GCC_except_table", "DW.ref.", "_ZT", "_ZGV")
IGNORED_NAMES = frozenset((
    # libc++ inline constant emitted into users of unordered containers.
    "_ZNSt3__119piecewise_constructE",
    # Private table from the vendored xxHash implementation.
    "_ZL12XXH3_kSecret",
))
READELF_MEMBER = re.compile(r"^File: .+\(([^()]*)\)$")


def tls_names(archive):
    output = subprocess.run(
        ["readelf", "--wide", "--symbols", archive],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout
    names = set()
    member = None
    for line in output.splitlines():
        match = READELF_MEMBER.match(line)
        if match:
            member = match.group(1)
            continue
        fields = line.split(None, 7)
        if (member is not None and len(fields) == 8 and fields[3] == "TLS"
                and fields[6] != "UND"):
            names.add((member, fields[7]))
    return names


def object_name(location):
    opening = location.rfind("[")
    if opening != -1 and location.endswith("]"):
        return location[opening + 1:-1]
    return location


def storage_symbols(archive):
    output = subprocess.run(
        ["nm", "--print-file-name", "--format=posix", "--print-size",
         "--defined-only", archive],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout
    symbols = []
    for line in output.splitlines():
        location, separator, rest = line.partition(": ")
        fields = rest.split()
        if not separator or len(fields) < 3:
            continue
        name, kind = fields[0], fields[1]
        if (kind not in STORAGE_TYPES or name in IGNORED_NAMES
                or name.startswith(IGNORED_PREFIXES)):
            continue
        symbols.append((object_name(location), name))
    return symbols


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: static_storage_gate.py ARCHIVE STAMP")
    archive, stamp = sys.argv[1:]
    tls = tls_names(archive)
    symbols = storage_symbols(archive)
    static = [symbol for symbol in symbols if symbol not in tls]
    thread = [symbol for symbol in symbols if symbol in tls]

    counts = collections.defaultdict(lambda: [0, 0])
    for location, _ in static:
        counts[location][0] += 1
    for location, _ in thread:
        counts[location][1] += 1

    print(f"static_storage_gate: static={len(static)}, "
          f"thread_local={len(thread)}, total={len(symbols)} (required: 0)")
    for location, (ordinary, per_thread) in sorted(
        counts.items(), key=lambda item: (-sum(item[1]), item[0])
    ):
        print(f"  {location}: static={ordinary}, thread_local={per_thread}")

    if static or thread:
        return 1
    with open(stamp, "w"):
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
