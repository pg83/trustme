#!/usr/bin/env python3
"""Fail while compiler object files contain static-storage state."""

import collections
import re
import subprocess
import sys


STORAGE_TYPES = frozenset("bBcCdDgGrRsSuVv")
MAX_STORAGE_OBJECTS = 0
MAX_WRITABLE_BYTES = 0

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
ALLOWED_IMMUTABLE = frozenset((
    # Generated Unicode normalization tables.
    ("unicode_nfc.cpp.o", "_ZN12_GLOBAL__N_19COMBININGE", "r"),
    ("unicode_nfc.cpp.o", "_ZN12_GLOBAL__N_114DECOMPOSITIONSE", "r"),
    ("unicode_nfc.cpp.o", "_ZN12_GLOBAL__N_112COMPOSITIONSE", "r"),
    # Embedded source emitted by the C backend.
    ("trans_codegen_c.cpp.o", "_ZL17CODEGEN_C_PRELUDE", "r"),
    # Parser and core-type lookup tables.
    ("coretypes.cpp.o", "_ZN12_GLOBAL__N_19CORETYPESE", "d"),
    ("parse_lex.cpp.o", "_ZN12_GLOBAL__N_18TOKENMAPE", "d"),
    ("parse_lex.cpp.o", "_ZN12_GLOBAL__N_111RWORDS_2015E", "d"),
    ("parse_lex.cpp.o", "_ZN12_GLOBAL__N_111RWORDS_2018E", "d"),
    # Allocator ABI metadata.
    ("trans_allocator.cpp.o", "ALLOCATOR_METHODS", "D"),
    ("trans_allocator.cpp.o", "_ZL28ALLOCATOR_METHODS_ARGS_alloc", "r"),
    ("trans_allocator.cpp.o", "_ZL30ALLOCATOR_METHODS_ARGS_dealloc", "r"),
    ("trans_allocator.cpp.o", "_ZL30ALLOCATOR_METHODS_ARGS_realloc", "r"),
    ("trans_allocator.cpp.o", "_ZL35ALLOCATOR_METHODS_ARGS_alloc_zeroed", "r"),
    ("trans_allocator.cpp.o", "GLOBAL_ALLOCATOR_LANG_ITEM", "R"),
    # Fixed lookup tables local to their consumers.
    ("synext_macro.cpp.o",
     "_ZZN12_GLOBAL__N_119x86ReservedRegisterERKNSt3__112basic_stringIcNS0_"
     "11char_traitsIcEENS0_9allocatorIcEEEEE8reserved", "d"),
    ("synext_macro.cpp.o",
     "_ZZN12_GLOBAL__N_120canonicalX86RegisterERKNSt3__112basic_stringIcNS0_"
     "11char_traitsIcEENS0_9allocatorIcEEEEbE7aliases", "d"),
    ("hir_typeck_helpers.cpp.o",
     "_ZZNK15TraitResolution22NextTraitGoalEvaluator20literalClassCanMatchERK"
     "13HIRSimplePathRK13HIRPathParams13HIRInferClassE8intPrims", "r"),
    ("hir_typeck_helpers.cpp.o",
     "_ZZNK15TraitResolution22NextTraitGoalEvaluator20literalClassCanMatchERK"
     "13HIRSimplePathRK13HIRPathParams13HIRInferClassE10floatPrims", "r"),
))

# Temporary migration exceptions. These are mutable and must disappear once
# their factories are threaded from the root compilation context.
ALLOWED_MUTABLE = frozenset((
    ("hir_path.cpp.o", "_ZZN12_GLOBAL__N_18internerEvE2in", "b"),
    ("rc_string.cpp.o", "_ZZN12_GLOBAL__N_18internerEvE2in", "b"),
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
        if not separator or len(fields) < 4:
            continue
        name, kind = fields[0], fields[1]
        if (kind not in STORAGE_TYPES or name in IGNORED_NAMES
                or name.startswith(IGNORED_PREFIXES)):
            continue
        symbols.append((object_name(location), name, kind, int(fields[3], 16)))
    return symbols


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: static_storage_gate.py ARCHIVE STAMP")
    archive, stamp = sys.argv[1:]
    tls = tls_names(archive)
    all_symbols = storage_symbols(archive)
    allowed_names = ALLOWED_IMMUTABLE | ALLOWED_MUTABLE
    allowed = [symbol for symbol in all_symbols
               if symbol[:3] in allowed_names
               and symbol[:2] not in tls]
    allowed_immutable_count = sum(
        symbol[:3] in ALLOWED_IMMUTABLE for symbol in allowed)
    allowed_mutable_count = sum(
        symbol[:3] in ALLOWED_MUTABLE for symbol in allowed)
    symbols = [symbol for symbol in all_symbols if symbol not in allowed]
    static = [symbol for symbol in symbols if symbol[:2] not in tls]
    thread = [symbol for symbol in symbols if symbol[:2] in tls]
    # r/R are read-only data.  Ambiguous weak/unique symbols are counted as
    # writable unless explicitly admitted above, so packing state into one
    # aggregate cannot make the byte metric smaller.
    writable_bytes = sum(symbol[3] for symbol in symbols
                         if symbol[:2] in tls or symbol[2] not in "rR")

    observed_allowed = {symbol[:3] for symbol in allowed}
    missing_allowed = allowed_names - observed_allowed
    duplicate_allowed = len(allowed) != len(observed_allowed)

    counts = collections.defaultdict(lambda: [0, 0, 0])
    for location, _, kind, size in static:
        counts[location][0] += 1
        if kind not in "rR":
            counts[location][2] += size
    for location, _, _, size in thread:
        counts[location][1] += 1
        counts[location][2] += size

    print(f"static_storage_gate: static={len(static)}, "
          f"thread_local={len(thread)}, total={len(symbols)}, "
          f"writable_bytes={writable_bytes}, "
          f"allowed_immutable={allowed_immutable_count}, "
          f"allowed_mutable={allowed_mutable_count} "
          f"(maximum: objects={MAX_STORAGE_OBJECTS}, "
          f"writable_bytes={MAX_WRITABLE_BYTES})")
    for location, (ordinary, per_thread, byte_count) in sorted(
        counts.items(), key=lambda item: (-(item[1][0] + item[1][1]), item[0])
    ):
        print(f"  {location}: static={ordinary}, thread_local={per_thread}, "
              f"writable_bytes={byte_count}")
        for _, name, kind, size in sorted(
            symbol for symbol in static + thread if symbol[0] == location
        ):
            print(f"    {kind} {size}: {name}")

    for location, name, kind in sorted(missing_allowed):
        print(f"static_storage_gate: stale immutable exception: "
              f"{location}: {kind} {name}", file=sys.stderr)
    if duplicate_allowed:
        print("static_storage_gate: duplicate immutable exception symbol", file=sys.stderr)

    if (len(symbols) > MAX_STORAGE_OBJECTS
            or writable_bytes > MAX_WRITABLE_BYTES
            or missing_allowed or duplicate_allowed):
        return 1
    with open(stamp, "w"):
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
