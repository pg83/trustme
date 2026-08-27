#!/usr/bin/env python3
"""Fail while compiler object files contain static or thread-local objects."""

import collections
import re
import subprocess
import sys


STORAGE_TYPES = frozenset("bBcCdDgGrRsSuVv")
MAX_STORAGE_OBJECTS = 0

# nm also exposes data emitted by the C++ ABI and the compiler itself.  None of
# these names denotes an object declared by the trustme sources.
IGNORED_PREFIXES = (".", "GCC_except_table", "DW.ref.", "_ZT", "_ZGV")
IGNORED_NAMES = frozenset((
    # libc++ inline constant emitted into users of unordered containers.
    "_ZNSt3__119piecewise_constructE",
    # Private table from the vendored xxHash implementation.
    "_ZL12XXH3_kSecret",
))

# These source-level objects are immutable POD lookup data.  They have no
# constructors, destructors, caches, registration side effects, or mutable
# state; keeping one shared copy is their intended representation.
ALLOWED_IMMUTABLE_NAMES = frozenset((
    # Generated Unicode normalization tables.
    "_ZN12_GLOBAL__N_19COMBININGE",
    "_ZN12_GLOBAL__N_114DECOMPOSITIONSE",
    "_ZN12_GLOBAL__N_112COMPOSITIONSE",
    # Embedded source emitted by the C backend.
    "_ZL17CODEGEN_C_PRELUDE",
    # Parser and core-type lookup tables.
    "_ZL9CORETYPES",
    "_ZL8TOKENMAP",
    "_ZL11RWORDS_2015",
    "_ZL11RWORDS_2018",
    # Allocator ABI metadata.
    "ALLOCATOR_METHODS",
    "_ZL28ALLOCATOR_METHODS_ARGS_alloc",
    "_ZL30ALLOCATOR_METHODS_ARGS_dealloc",
    "_ZL30ALLOCATOR_METHODS_ARGS_realloc",
    "_ZL35ALLOCATOR_METHODS_ARGS_alloc_zeroed",
    "GLOBAL_ALLOCATOR_LANG_ITEM",
    # Fixed lookup tables local to their consumers.
    ("_ZZN12_GLOBAL__N_119x86ReservedRegisterERKNSt3__112basic_stringIcNS0_"
     "11char_traitsIcEENS0_9allocatorIcEEEEE8reserved"),
    ("_ZZN12_GLOBAL__N_120canonicalX86RegisterERKNSt3__112basic_stringIcNS0_"
     "11char_traitsIcEENS0_9allocatorIcEEEEbE7aliases"),
    ("_ZZN12_GLOBAL__N_123TypeRestrictiveOrdering14getOrderingPtrERK4SpanRK7"
     "ContextPK11HIRTypeDataS9_RbbE11tagOrdering"),
    ("_ZZNK22NextTraitGoalEvaluator20literalClassCanMatchERK13HIRSimplePathRK"
     "13HIRPathParams13HIRInferClassE8intPrims"),
    ("_ZZNK22NextTraitGoalEvaluator20literalClassCanMatchERK13HIRSimplePathRK"
     "13HIRPathParams13HIRInferClassE10floatPrims"),
    "_ZZN7Mangler7fmtNameEPKcE3HEX",
    "_ZZN12_GLOBAL__N_112mangleFinishERN3stl13StringBuilderEE3HEX",
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
    all_symbols = storage_symbols(archive)
    allowed = [symbol for symbol in all_symbols
               if symbol[1] in ALLOWED_IMMUTABLE_NAMES]
    symbols = [symbol for symbol in all_symbols
               if symbol[1] not in ALLOWED_IMMUTABLE_NAMES]
    static = [symbol for symbol in symbols if symbol not in tls]
    thread = [symbol for symbol in symbols if symbol in tls]

    counts = collections.defaultdict(lambda: [0, 0])
    for location, _ in static:
        counts[location][0] += 1
    for location, _ in thread:
        counts[location][1] += 1

    print(f"static_storage_gate: static={len(static)}, "
          f"thread_local={len(thread)}, total={len(symbols)} "
          f"allowed_immutable={len(allowed)} (maximum: {MAX_STORAGE_OBJECTS})")
    for location, (ordinary, per_thread) in sorted(
        counts.items(), key=lambda item: (-sum(item[1]), item[0])
    ):
        print(f"  {location}: static={ordinary}, thread_local={per_thread}")

    if len(symbols) > MAX_STORAGE_OBJECTS:
        return 1
    with open(stamp, "w"):
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
