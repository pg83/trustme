import hashlib
import json
import re
from pathlib import Path

import build

# Build description for rustc (the trustme Rust compiler).
# The compiler sources live flat under bin/rustc/; C++26, links the external
# platform library and zlib.

build.flags.allow({
    "group": {
        "descr": "zero-based lite test partition to include",
        "default": "",
    },
    "group_count": {
        "descr": "total number of lite test partitions",
        "default": "",
    },
    "system_rustc": {
        "descr": "external rustc used for the semantic test corpus",
        "default": "",
    },
    "system_cargo": {
        "descr": "external Cargo paired with -Dsystem_rustc",
        "default": "cargo",
    },
    "system_linker": {
        "descr": "native linker used by the external rustc",
        "default": "gcc",
    },
})


def parse_test_partition():
    group_value = build.flags.group
    group_count_value = build.flags.group_count
    if bool(group_value) != bool(group_count_value):
        raise RuntimeError("-Dgroup and -Dgroup_count must be specified together")
    if not group_value:
        return None
    try:
        group_index = int(group_value)
        group_count = int(group_count_value)
    except ValueError as error:
        raise RuntimeError("-Dgroup and -Dgroup_count must be integers") from error
    if group_count <= 0 or group_index < 0 or group_index >= group_count:
        raise RuntimeError(
            "test partition requires 0 <= group < group_count and group_count > 0"
        )
    return group_index, group_count


test_partition = parse_test_partition()
system_rustc_mode = bool(build.flags.system_rustc)

build.includes += ["$(S)/bin/rustc", "$(B)/gen"]

build.cxxflags += [
    "-std=c++26",
    "-O2",
    "-g",
]

# Keep the platform library in the same imported build graph as its consumers:
# libstd owns its source discovery and compile flags, while the parent graph
# supplies reproducible paths and links the resulting archive.
platform_libstd = import_build(
    "ext/libstd/build.py",
    "libstd.a",
    extra_cflags=[
        "-Wno-error",
        "-ffile-prefix-map=$(S)=.",
        "-ffile-prefix-map=$(B)=.",
    ],
)
# `libstd` is already the public target for the Rust standard-library test
# artifact below. Preserve that interface and expose the C++ library explicitly.
platform_libstd.name = "platform_libstd"

# The ThinLTO flavour of the platform library, linked into the ThinLTO rustc
# so ObjPool and the other hot-path primitives inline across the boundary.
platform_libstd_lto = import_build(
    "ext/libstd/build.py",
    "libstd.a",
    extra_cflags=[
        "-Wno-error",
        "-ffile-prefix-map=$(S)=.",
        "-ffile-prefix-map=$(B)=.",
        "-flto=thin",
    ],
    extra_cxxflags=["-flto=thin"],
    namespace="ext/libstd-lto",
)
platform_libstd_lto.name = "platform_libstd_lto"

RUSTC_CPP_SRC = build.glob("$(S)/bin/rustc/*.cpp")
SRC = RUSTC_CPP_SRC
# Compiler C++ unit tests live next to the code they test (x.cpp -> x_ut.cpp)
# and are linked into the rustc_ut runner, not into the compiler.
UT_SRC = sorted(s for s in SRC if s.endswith("_ut.cpp"))
SRC = [s for s in SRC if not s.endswith("_ut.cpp")]
# A sanitizer owns malloc; the process allocator stays out of a sanitized build.
SANITIZED = any(
    flag.startswith("-fsanitize=") for flag in (*build.cflags, *build.cxxflags)
)
SRC = [s for s in SRC if not (SANITIZED and s.endswith("/malloc.cpp"))]
# The allocator's object goes first on the link line.  A toolchain wrapper
# may name libc before the objects, and lld resolves such backward references
# eagerly: the first object that calls free would then extract libc's own
# free.o, and the allocator's definition later on the line collides with it.
SRC = sorted(SRC, key=lambda s: not s.endswith("/malloc.cpp"))

# The tu_gen.py sample fixture is exercised by tagged_union_sample_ut.cpp in
# the rustc_ut runner; it is not part of the compiler.
TU_SAMPLE_SRC = "$(S)/bin/rustc/tagged_union_sample.cpp"
SRC = [s for s in SRC if s != TU_SAMPLE_SRC]

# The Unicode tables canonical composition needs come from Python's own data,
# generated rather than checked in.
UNICODE_NFC_TABLES = "$(B)/gen/unicode_nfc_tables.inc"
unicode_nfc_tables = command(
    name="unicode_nfc_tables",
    inputs=["$(S)/dev/gen_unicode_nfc.py"],
    outputs=[UNICODE_NFC_TABLES],
    cmd=["python3", "$(S)/dev/gen_unicode_nfc.py", UNICODE_NFC_TABLES],
    descr="GN",
)

CODEGEN_C_PRELUDE = "$(B)/gen/codegen_c_prelude.h"
codegen_c_prelude = command(
    name="codegen_c_prelude",
    inputs=[
        "$(S)/bin/rustc/prelude.inc",
        "$(S)/dev/embed_text.py",
    ],
    outputs=[CODEGEN_C_PRELUDE],
    cmd=[
        "python3", "$(S)/dev/embed_text.py",
        "$(S)/bin/rustc/prelude.inc", CODEGEN_C_PRELUDE,
        "CODEGEN_C_PRELUDE",
    ],
    descr="GN",
)


# Tagged unions are generated: each bin/rustc/xxx.tu yields $(B)/gen/xxx_tu.h
# (thin class definitions, included mid-file by the hand-written xxx.h) and
# $(B)/gen/xxx_tu.cpp (every method body; includes xxx.h for context, listed
# here as a scanned input so header changes rebuild it).
TU_GEN_TOOL = "$(S)/dev/tu_gen.py"
tu_generated_srcs = []
tu_context_re = re.compile(r'^\s*context\(\s*["\']([^"\']+)["\']', re.M)
for tu_src in sorted(build.glob("$(S)/bin/rustc/*.tu")):
    tu_stem = tu_src.rsplit("/", 1)[1][:-len(".tu")]
    tu_gen_h = f"$(B)/gen/{tu_stem}_tu.h"
    tu_gen_cpp = f"$(B)/gen/{tu_stem}_tu.cpp"
    # The generated cpp includes its context header (see tu_gen.py): xxx.h by
    # default, or the header a context("...") call names for sub-units.
    tu_text = Path(__file__).parent.joinpath("bin/rustc", f"{tu_stem}.tu").read_text()
    tu_context_match = tu_context_re.search(tu_text)
    tu_context = tu_context_match.group(1) if tu_context_match else f"{tu_stem}.h"
    # local() units are textually included by their client cpp (header at the
    # union's spot, bodies at the end of the file) and are not compiled
    # separately.
    tu_local = re.search(r"^\s*local\(\)", tu_text, re.M) is not None
    command(
        name=f"{tu_stem}_tu",
        inputs=[tu_src, TU_GEN_TOOL],
        outputs=[tu_gen_h, tu_gen_cpp],
        cmd=["python3", TU_GEN_TOOL, tu_src, tu_gen_h, tu_gen_cpp],
        descr="TU",
    )
    if not tu_local:
        # The generated cpp includes its context header and output.h (see
        # tu_gen.py); both are declared so their include closures are inputs.
        tu_generated_srcs.append(
            {"src": tu_gen_cpp, "inputs": [f"$(S)/bin/rustc/{tu_context}", "$(S)/bin/rustc/output.h"]}
        )

# Real compiler unions link into the compiler (and, via SRC, into rustc_ut);
# the tagged_union_sample fixture stays out of the compiler binary.
tu_compiler_srcs = [
    entry for entry in tu_generated_srcs
    if not (entry if isinstance(entry, str) else entry["src"]).endswith(
        "/tagged_union_sample_tu.cpp")
]


def compiler_source(source, *generated_inputs):
    inputs = list(generated_inputs)
    if source.endswith("/trans_codegen_c.cpp"):
        inputs.append(CODEGEN_C_PRELUDE)
    if source.endswith("/unicode_nfc.cpp"):
        inputs.append(UNICODE_NFC_TABLES)
    return {"src": source, "inputs": inputs} if inputs else source

if system_rustc_mode:
    rustc = command(
        name="rustc",
        local=True,
        inputs=["$(S)/tst/system_rustc.py"],
        outputs=["$(B)/bin/rustc"],
        cmd=[
            "python3", "$(S)/tst/system_rustc.py", "launcher",
            build.flags.system_rustc, build.flags.system_linker,
            "$(B)/bin/rustc",
        ],
        descr="SR",
        color="cyan",
    )
else:
    rustc = program(
        srcs=[*[compiler_source(source) for source in SRC], *tu_compiler_srcs],
        name="rustc",
        output="$(B)/bin/rustc",
        # ThinLTO: the codebase deliberately keeps bodies out of headers
        # (thin _tu.h, out-of-line accessors) and leaves cross-TU inlining
        # to LTO; measured ~23% faster libcore compiles. The LTO flavour of
        # the platform library joins in so ObjPool inlines too.
        deps=[platform_libstd_lto, codegen_c_prelude, unicode_nfc_tables],
        cxxflags=["-flto=thin"],
        ldflags=["-lzstd", "-flto=thin"],
    )

    rustc_debug = program(
        srcs=[*[compiler_source(source) for source in SRC], *tu_compiler_srcs],
        name="rustc.debug",
        output="$(B)/bin/rustc.debug",
        deps=[platform_libstd_lto, codegen_c_prelude, unicode_nfc_tables],
        cppflags=["-DTRUSTME_DEBUG=1"],
        cxxflags=["-flto=thin"],
        ldflags=["-lzstd", "-flto=thin"],
    )

    # The production compiler uses ThinLTO, which can hide static storage by
    # internalising or deleting symbols.  Build the same translation units as
    # a plain archive so the zero-storage unit gate can inspect every object.
    rustc_storage_objects = library(
        srcs=[*[compiler_source(source) for source in SRC], *tu_compiler_srcs],
        name="rustc_storage_objects",
        output="$(B)/tst/unit/rustc_storage_objects.a",
        deps=[platform_libstd, codegen_c_prelude, unicode_nfc_tables],
        cxxflags=["-fno-lto"],
    )

# Reference vectors for the float128 unit tests are produced by a generator
# node, not checked in.
FLOAT128_VECTORS = "$(B)/gen/float128_ut_vectors.inc"
float128_ut_vectors = command(
    name="float128_ut_vectors",
    inputs=["$(S)/dev/gen_float128_vectors.py"],
    outputs=[FLOAT128_VECTORS],
    cmd=["python3", "$(S)/dev/gen_float128_vectors.py", FLOAT128_VECTORS],
    descr="GN",
)

# The compiler's C++ unit-test runner: every bin/rustc/*_ut.cpp registers its
# STD_TEST cases, linked against the compiler objects (minus main) and the
# platform library's test framework.
rustc_ut = program(
    name="rustc_ut",
    srcs=[
        "$(S)/tst/unit/rustc_ut_main.cpp",
        # The tu_gen.py sample fixture and its generated bodies. Every .tu in
        # the tree today is a sample; once real compiler unions migrate to
        # .tu, their generated sources join SRC and this list keeps only the
        # sample.
        TU_SAMPLE_SRC,
        *tu_generated_srcs,
        *[
            compiler_source(
                s,
                *([FLOAT128_VECTORS] if s.endswith("/float128_ut.cpp") else []),
            )
            for s in UT_SRC
        ],
        *[
            compiler_source(s)
            for s in SRC
            if not s.endswith("/main_bindings.cpp") and not s.endswith("/malloc.cpp")
        ],
    ],
    output="$(B)/tst/unit/rustc_ut",
    deps=[platform_libstd, float128_ut_vectors, codegen_c_prelude, unicode_nfc_tables],
    ldflags=["-lzstd"],
)

node_cast_test = program(
    name="node_cast_test",
    srcs=["$(S)/tst/unit/test_node_cast.cpp"],
    output="$(B)/tst/unit/node_cast_test",
    deps=[platform_libstd],
)

ident_ordering_test = program(
    name="ident_ordering_test",
    srcs=[
        "$(S)/tst/unit/test_ident_ordering.cpp",
        "$(S)/bin/rustc/ident.cpp",
        "$(S)/bin/rustc/rc_string.cpp",
    ],
    output="$(B)/tst/unit/ident_ordering_test",
    deps=[platform_libstd],
)

# cargo: Cargo-compatible package resolver and trustme build driver, written in
# Go. Dependencies are checked in under bin/cargo/vendor, so this node is
# offline. Rust 1.90 source adjustments belong to std_src below; Cargo has no
# toolchain-specific override configuration.
CARGO_SOURCES = (
    build.glob("$(S)/bin/cargo/**/*.go")
    + build.glob("$(S)/bin/cargo/**/*.s")
    + ["$(S)/bin/cargo/go.mod", "$(S)/bin/cargo/go.sum", "$(S)/bin/cargo/vendor/modules.txt"]
)

cargo = command(
    name="cargo",
    # Go lives on this machine only.
    local=True,
    inputs=CARGO_SOURCES,
    outputs=["$(B)/bin/cargo"],
    cmd=[
        "go", "build",
        "-o", "$(B)/bin/cargo",
        ".",
    ],
    cwd="$(S)/bin/cargo",
    env={
        "CGO_ENABLED": "0",
        "GOCACHE": "$(B)/gocache",
        "GOFLAGS": "-mod=vendor",
        "GOTOOLCHAIN": "local",
    },
    descr="GO",
)

# The same sources under `go test`: the only gate over Cargo's own graph - the
# resolver, which targets a build selects, where their outputs are installed,
# and the message stream `--message-format=json` writes. A real project sees
# those only through whatever it fails to build, half an hour later.
cargo_test = command(
    name="cargo_test",
    local=True,
    inputs=CARGO_SOURCES + [
        _path for _path in build.glob("$(S)/bin/cargo/testdata/**/*")
        if Path(__file__).parent.joinpath(_path[len("$(S)/"):]).is_file()
    ],
    outputs=["$(B)/tst/unit/cargo.stamp"],
    cmd=[
        ["go", "test", "-timeout", "120s", "./..."],
        ["sh", "-c", "> $(B)/tst/unit/cargo.stamp"],
    ],
    cwd="$(S)/bin/cargo",
    env={
        "CGO_ENABLED": "0",
        "GOCACHE": "$(B)/gocache",
        "GOFLAGS": "-mod=vendor",
        "GOTOOLCHAIN": "local",
    },
    descr="GO",
    color="green",
)

# --- tests -----------------------------------------------------------------
# A test is one real project, built by our toolchain and exercised. The graph
# is tar-based: each node produces a single archive, and downstream nodes
# unpack what they need (the build engine only promotes declared file outputs).
#
# The standard library is a *shared* pair of nodes — fetched and built once,
# then depended on by every project. See tst/README.md.
#
# Real-project and unusually long upstream tests are outside the fast `test`
# gate. Run them together with `./build slow_tests`, or select one target such
# as `./build resvg` directly.

TOOLCHAIN_ENV = {
    "RUSTC": "$(B)/bin/rustc",
    "CARGO": "$(B)/bin/cargo",
}
SYSTEM_TEST_ENV = {"TRUSTME_SYSTEM_RUSTC": "1"} if system_rustc_mode else {}

# All node scripts are Python and share tst/lib.py. Compiler invocations are
# prefixed with tst/wrap_gdb.py: a signal death is rerun under gdb (assumed
# available) so the node's log ends with symbolised backtraces.
TIMEOUT_SCRIPT = "$(S)/dev/timeout.py"
TIMEOUT_INPUT = [TIMEOUT_SCRIPT]
TESTS_LIB = [*TIMEOUT_INPUT, "$(S)/tst/lib.py", "$(S)/tst/wrap_gdb.py"]
# Every budget below is spent driving the compiler this build produced, and a
# sanitized one spends several times the instructions of a plain one on the same
# input: the libstd node takes 248s built plainly and 1308s built with
# -fsanitize=address, on the same quiet machine with the same 40 jobs. A budget
# is here to catch a hang, not to time the sanitizer, so each one stretches by
# that measured factor when the toolchain carries a sanitizer, and is left
# exactly as measured when it does not.
TIMEOUT_SCALE = 5 if SANITIZED else 1


def budget(minutes=0, seconds=0):
    total = (minutes * 60 + seconds) * TIMEOUT_SCALE
    return ["python3", TIMEOUT_SCRIPT, f"{total}s"]


# Bound the whole test node, including compilation performed by adapters. The
# in-tree wrapper gives ix and Ubuntu identical process-group semantics.
TEST_TIMEOUT = budget(seconds=60)
# Exercism's convention is that every case after the first carries `#[ignore]`,
# for a learner to enable one at a time, so the adapter runs them with
# `--include-ignored` - which is what the exercise is. That pulls in the
# exhaustive ones: `palindrome-products` alone runs `palindrome_products(1000,
# 9999)` twice, 40.5M iterations each, and takes 52.3s of a quiet machine
# against the next slowest exercise at 10.6s. Compiling is a constant 5s; the
# cost is all in the run, and the two heavy cases already run in parallel. Sized
# so the outlier has room on a machine the rest of the corpus is also using.
EXERCISM_TIMEOUT = budget(minutes=3)
# A real project contains a full Cargo graph and starts from archive inputs in a
# fresh directory, so unlike a unit node it cannot reuse a materialised CAS.
PROJECT_TIMEOUT = budget(minutes=5)
# A handful of corpus nodes are heavy by construction: trybuild and zerocopy
# compile their own dependency graph and then a second generated Cargo
# workspace of UI cases, and rayon carries a test suite far larger than the
# library - 5m54s alone and 9m01s with the load average averaging 76, the
# longest thing on this budget and what its size is set by. addr2line, syn and
# pest are heavy only next to the ordinary budget: alone they finish in 3 to 4
# minutes, but the corpus does not run on an idle machine, and at a load
# average between 77 and 97 on 78 cores those three take 5 to 7 minutes, which
# is all the five-minute budget was killing them for. Keep the ordinary project
# budget tight and hand this one only to the nodes that have been measured to
# need it.
NESTED_PROJECT_TIMEOUT = budget(minutes=15)
# resvg is the node a busy machine stretches furthest. It compiles the widest
# dependency graph in the corpus, 150 units, and then renders a reference suite
# through the binary it built, and every one of those units wants a core of its
# own: 2m53s at a load average of 13, 11m01s on a machine already busy, 14m56s
# with the load average averaging 58 and peaking at 143 - four seconds inside
# the nested budget, which is how the corpus was losing it - and 23m07s with it
# averaging 162, exiting 0 every time. Sized at twice that worst measurement,
# so that a real hang is still distinguishable from a contended machine.
HEAVY_PROJECT_TIMEOUT = budget(minutes=45)
# clap is the longest node in the corpus, and the only one that runs a crate's
# whole upstream suite: nine test binaries, the largest of them holding 1586
# tests, every one linked from C++ the backend writes - and now example_tests
# and ui_tests too, each of which starts a second Cargo build of the crate.
# Those two came out from under --xfail-target once our Cargo learned to emit
# the message stream they read, and they are what took the node past the thirty
# minutes it used to be given. With 40 build jobs: 22m41s on a quiet machine,
# 39m25s with the load average averaging 101, and 50m28s with it averaging 96
# and peaking at 180, exiting 0 every time. Sized at twice that worst
# measurement, on the same rule as the heavy budget, so that a real hang is
# still distinguishable from a contended machine.
FULL_SUITE_PROJECT_TIMEOUT = budget(minutes=100)
# A from-scratch standard-library build is intentionally much heavier than a
# single test, but it must not leave the graph occupied indefinitely.
LIBSTD_TIMEOUT = budget(minutes=10)

# std_src: fetch + adjust the rust-1.90 source, add the shim, pack it.
std_src = command(
    name="std_src",
    local=True,
    inputs=["$(S)/tst/std/fetch.py"] + TESTS_LIB,
    outputs=["$(B)/tst/rust-src.tar"],
    cmd=[
        *(LIBSTD_TIMEOUT if system_rustc_mode else []),
        "python3", "$(S)/tst/std/fetch.py",
        "$(B)/tst/rust-src.tar",
    ],
    descr="RS",
    color="cyan",
)

# System rustc obtains its standard library from its own sysroot. The empty
# archive preserves the adapters' interface while making their `-L` harmless.
if system_rustc_mode:
    libstd = command(
        name="libstd",
        local=True,
        inputs=["$(S)/tst/system_rustc.py"],
        outputs=["$(B)/tst/libstd.tar"],
        cmd=[
            "python3", "$(S)/tst/system_rustc.py", "empty-libstd",
            "$(B)/tst/libstd.tar",
        ],
        descr="SL",
        color="cyan",
    )
else:
    # Build the standard library (+ libproc_macro) once, from that source.
    libstd = command(
        name="libstd",
        # Reads the multi-gigabyte Rust source archive: that stays here.
        local=True,
        inputs=(
            ["$(S)/tst/std/build.py"]
            + build.glob("$(S)/lib/proc_macro/**/*.rs")
            + build.glob("$(S)/lib/proc_macro/Cargo.toml")
            + TESTS_LIB
        ),
        outputs=["$(B)/tst/libstd.tar"],
        cmd=[
            *LIBSTD_TIMEOUT,
            "python3", "$(S)/tst/std/build.py",
            "$(B)/tst/rust-src.tar", "$(B)/tst/libstd.tar",
            "$(S)/lib/proc_macro/Cargo.toml",
        ],
        deps=[std_src, rustc, cargo],
        env=TOOLCHAIN_ENV,
        descr="LS",
        color="cyan",
    )

rust_test_helpers = command(
    name="rust_test_helpers",
    local=True,
    inputs=["$(S)/tst/rust_1_90/build_native.py", *TESTS_LIB],
    outputs=["$(B)/tst/rust_1_90/native/librust_test_helpers.a"],
    cmd=[
        "python3",
        "$(S)/tst/rust_1_90/build_native.py",
        "$(B)/tst/rust-src.tar",
        "$(B)/tst/rust_1_90/native/librust_test_helpers.a",
    ],
    deps=[std_src],
    descr="RN",
    color="cyan",
)

if system_rustc_mode:
    rust_lib_dependencies = command(
        name="rust_lib_dependencies",
        local=True,
        inputs=(
            [
                "$(S)/tst/rust_lib/build_system_dependencies.py",
                "$(S)/tst/rust_lib/dependencies/Cargo.toml",
                "$(S)/tst/rust_lib/dependencies/Cargo.lock",
            ]
            + build.glob("$(S)/tst/rust_lib/dependencies/src/**/*.rs")
            + TESTS_LIB
        ),
        outputs=["$(B)/tst/rust-lib-dependencies.tar"],
        cmd=[
            *LIBSTD_TIMEOUT,
            "python3",
            "$(S)/tst/rust_lib/build_system_dependencies.py",
            "$(B)/tst/rust-src.tar",
            "$(B)/tst/rust-lib-dependencies.tar",
        ],
        deps=[std_src, rustc],
        env={
            "RUSTC": "$(B)/bin/rustc",
            "CARGO": build.flags.system_cargo,
        },
        descr="SD",
        color="cyan",
    )
else:
    rust_lib_dependencies = command(
        name="rust_lib_dependencies",
        local=True,
        inputs=(
            [
                "$(S)/tst/rust_lib/build_dependencies.py",
                "$(S)/tst/rust_lib/dependencies/Cargo.toml",
                "$(S)/tst/rust_lib/dependencies/Cargo.lock",
            ]
            + build.glob("$(S)/tst/rust_lib/dependencies/src/**/*.rs")
            + TESTS_LIB
        ),
        outputs=["$(B)/tst/rust-lib-dependencies.tar"],
        cmd=[
            "python3",
            "$(S)/tst/rust_lib/build_dependencies.py",
            "$(B)/tst/rust-src.tar",
            "$(B)/tst/libstd.tar",
            "$(B)/tst/rust-lib-dependencies.tar",
        ],
        deps=[std_src, libstd, rustc, cargo],
        env=TOOLCHAIN_ENV,
        descr="LD",
        color="cyan",
    )

project_tests = []


def add_project_test(
    name,
    url,
    rev,
    *,
    manifest=".",
    vendor_manifest=None,
    adapter="$(S)/tst/test_project.py",
    adapter_args=(),
    adapter_inputs=(),
    lockfile=None,
    vendor_sync=(),
    timeout=PROJECT_TIMEOUT,
):
    """Add the source, vendor and build+test nodes for one pinned project."""
    if vendor_manifest is None:
        vendor_manifest = manifest

    source_archive = f"$(B)/tst/{name}-src.tar"
    vendor_archive = f"$(B)/tst/{name}-vendor.tar.zst"
    stamp = f"$(B)/tst/{name}.stamp"

    source_inputs = ["$(S)/tst/git_src.py"] + TESTS_LIB
    source_cmd = [
        "python3", "$(S)/tst/git_src.py", url, rev, source_archive,
    ]
    if lockfile:
        source_inputs.append(lockfile)
        source_cmd.extend([lockfile, vendor_manifest])

    source = command(
        name=name + "_src",
        local=True,
        inputs=source_inputs,
        outputs=[source_archive],
        cmd=source_cmd,
        descr="RS",
        color="magenta",
    )

    vendor = command(
        name=name + "_vendor",
        local=True,
        inputs=["$(S)/tst/vendor.py"] + TESTS_LIB,
        outputs=[vendor_archive],
        cmd=[
            "python3", "$(S)/tst/vendor.py",
            source_archive, vendor_manifest, vendor_archive, *vendor_sync,
        ],
        deps=[source, cargo],
        env={"CARGO": "$(B)/bin/cargo"},
        descr="VN",
        color="magenta",
    )

    # Real-project builds exercise our Cargo/toolchain integration, not the
    # semantic Rust corpus, so they do not exist in system-rustc mode.
    if system_rustc_mode:
        return None

    target = command(
        name=name,
        inputs=[adapter, *adapter_inputs] + TESTS_LIB,
        outputs=[stamp],
        cmd=[
            [
                *timeout,
                "python3", adapter,
                source_archive, vendor_archive, "$(B)/tst/libstd.tar",
                manifest, *adapter_args,
            ],
            [*TEST_TIMEOUT, "sh", "-c", f"> {stamp}"],
        ],
        deps=[source, vendor, libstd, rustc, cargo],
        env=TOOLCHAIN_ENV,
        descr="TS",
        color="magenta",
    )
    project_tests.append(target)
    return target


resvg = add_project_test(
    name="resvg",
    url="https://github.com/linebender/resvg.git",
    rev="08c79a3148df4ce8ab08fca72204b142b95423dd",
    manifest="crates/resvg",
    vendor_manifest=".",
    adapter="$(S)/tst/build_project.py",
    adapter_args=["python3", "$(S)/tst/resvg/run.py", "@BIN@"],
    adapter_inputs=["$(S)/tst/resvg/run.py"],
    timeout=HEAVY_PROJECT_TIMEOUT,
)

base64 = add_project_test(
    name="base64",
    url="https://github.com/marshallpierce/rust-base64.git",
    rev="069bf7067b949f5c0a92b6ceb82492920502f2c2",
    timeout=NESTED_PROJECT_TIMEOUT,
)

bitflags = add_project_test(
    name="bitflags",
    url="https://github.com/bitflags/bitflags.git",
    rev="f92a2921b41644b02ca5d50a6ace542e309e6a6f",
    lockfile="$(S)/tst/projects/bitflags/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

itertools = add_project_test(
    name="itertools",
    url="https://github.com/rust-itertools/itertools.git",
    rev="d5084d15e959b85d89a49e5cd33ad6267bc541a3",
    lockfile="$(S)/tst/projects/itertools/Cargo.lock",
)

clap = add_project_test(
    name="clap",
    url="https://github.com/clap-rs/clap.git",
    rev="3bd502024e45cc9abef690f28783d76a9ce33500",
    timeout=FULL_SUITE_PROJECT_TIMEOUT,
)

clap_2_33_3 = add_project_test(
    name="clap_2_33_3",
    url="https://github.com/clap-rs/clap.git",
    rev="33bebeda52b52c6f643b4ed6fa880671ba0ab80a",
    lockfile="$(S)/tst/projects/clap_2_33_3/Cargo.lock",
)

syn_0_15_44 = add_project_test(
    name="syn_0_15_44",
    url="https://github.com/dtolnay/syn.git",
    rev="6d798b63c255e90b7b1dbbfb3707fdce1704a18d",
    lockfile="$(S)/tst/projects/syn_0_15_44/Cargo.lock",
    adapter_args=["--release", "--all-features"],
    timeout=NESTED_PROJECT_TIMEOUT,
)

ron_0_4_2 = add_project_test(
    name="ron_0_4_2",
    url="https://github.com/ron-rs/ron.git",
    rev="be6bebab49d29bf3cf0fcf1c96fb870597e2c7b9",
    lockfile="$(S)/tst/projects/ron_0_4_2/Cargo.lock",
    adapter_args=["--xfail", "test_nul_in_string"],
)

console_0_7_7 = add_project_test(
    name="console_0_7_7",
    url="https://github.com/mitsuhiko/console.git",
    rev="9f62b487585476f7a5ba85cd6a2b109d6d15a92f",
    lockfile="$(S)/tst/projects/console_0_7_7/Cargo.lock",
)

insta_0_8_2 = add_project_test(
    name="insta_0_8_2",
    url="https://github.com/mitsuhiko/insta.git",
    rev="9373b2669c6c2ae44ab85afe24acb5369f839100",
    lockfile="$(S)/tst/projects/insta_0_8_2/Cargo.lock",
    adapter_args=[
        "--xfail", "test_unnamed_yaml_vector",
        "--xfail", "test_yaml_vector",
        "--xfail", "test_yaml_inline",
        "--xfail", "test_yaml_inline_redacted",
        "--xfail", "test_with_random_value",
    ],
    timeout=NESTED_PROJECT_TIMEOUT,
)

rayon_1_12_0 = add_project_test(
    name="rayon_1_12_0",
    url="https://github.com/rayon-rs/rayon.git",
    rev="c9ced185ae3508246a9eb70c8407a1199bb1b77f",
    lockfile="$(S)/tst/projects/rayon_1_12_0/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

crossbeam_utils_0_8_22 = add_project_test(
    name="crossbeam_utils_0_8_22",
    url="https://github.com/crossbeam-rs/crossbeam.git",
    rev="9b56303b8aa9ff8ec5bbebb9d2da05e034977889",
    manifest="crossbeam-utils",
    lockfile="$(S)/tst/projects/crossbeam_utils_0_8_22/Cargo.lock",
)

gimli_0_32_3 = add_project_test(
    name="gimli_0_32_3",
    url="https://github.com/gimli-rs/gimli.git",
    rev="8bc8e622fcb9be20fc9f03c96bc6335d936b869d",
    adapter_args=["--no-default-features", "--features", "read"],
    lockfile="$(S)/tst/projects/gimli_0_32_3/Cargo.lock",
)

addr2line_0_25_1 = add_project_test(
    name="addr2line_0_25_1",
    url="https://github.com/gimli-rs/addr2line.git",
    rev="f02db009deb9b441818afa49cb1b17453c1e4243",
    adapter_args=["--no-default-features"],
    timeout=NESTED_PROJECT_TIMEOUT,
)

backtrace_0_3_76 = add_project_test(
    name="backtrace_0_3_76",
    url="https://github.com/rust-lang/backtrace-rs.git",
    rev="775f6a1ba62e7d35a1fac76e64c61d9d4687b5f2",
    adapter_args=[
        "--xfail", "smoke_test_frames",
        "--xfail-target", "current-exe-mismatch",
    ],
)

pest_2_9_0 = add_project_test(
    name="pest_2_9_0",
    url="https://github.com/pest-parser/pest.git",
    rev="d9b29b61da505daafd028c19547366ace1ade7df",
    manifest="pest",
    vendor_manifest="pest",
    lockfile="$(S)/tst/projects/pest_2_9_0/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

combine = add_project_test(
    name="combine",
    url="https://github.com/Marwes/combine.git",
    rev="50a71afa1c88e8564e0220a6e0625dd16a2302a2",
)

camino_1_1_12 = add_project_test(
    name="camino_1_1_12",
    url="https://github.com/camino-rs/camino.git",
    rev="e5edcb948d31dc66000a560725ed9f22c98672f3",
    adapter_args=["--all-features"],
    lockfile="$(S)/tst/projects/camino_1_1_12/Cargo.lock",
)

# `persisted_cases_do_not_count_towards_total_cases` and
# `failing_cases_persisted_and_reloaded` (proptest/src/test_runner/runner.rs)
# both name the crate directory's `persistence-test.txt` as their failure
# persistence file and each begins by deleting it, so run side by side one
# test reads or truncates the other's file and either may fail. The race is
# upstream's: run as a pair under rustc 1.90 they collide 19 times in 20.
# The suite does not run them as a pair. Since `--test` lays its cases out in
# rustc's order they sit at 1488 and 1495 of 1507 rather than adjacent, and
# they are far enough apart that most runs miss it - but not all: the first
# full run after the harness thread came off lost
# `persisted_cases_do_not_count_towards_total_cases` at 1506 of 1507. So skip
# one of the pair and keep the parallelism, rather than serialising 1507 tests
# for the sake of two. The other half of the pair,
# `failing_cases_persisted_and_reloaded`, keeps the persistence path covered.
# Building the crate is 6 minutes since a scope's unwind cleanup became one
# shared chain, and the suite runs in 10 to 18 more, against the 57 minutes
# one harness thread cost.
proptest_1_11_0 = add_project_test(
    name="proptest_1_11_0",
    url="https://github.com/proptest-rs/proptest.git",
    rev="7f1367f9a4dc8440c47b93166a38ed064f63ea8c",
    manifest="proptest",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/proptest_1_11_0/Cargo.lock",
    adapter_args=["--", "--skip", "persisted_cases_do_not_count_towards_total_cases"],
    # Measured in parallel from cold archives at a load average of 84:
    # 1051s, 1000s and 628s, exiting 0 every time. Sized at twice the worst
    # of those on the same rule as the heavier budgets, so that a real hang
    # is still distinguishable from a contended machine.
    timeout=HEAVY_PROJECT_TIMEOUT,
)

alloca = add_project_test(
    name="alloca",
    url="https://github.com/playXE/alloca-rs.git",
    rev="1a5ff4220155da43390f7f7ee940cb508d3db262",
    lockfile="$(S)/tst/projects/alloca/Cargo.lock",
)

zerocopy = add_project_test(
    name="zerocopy",
    url="https://github.com/google/zerocopy.git",
    rev="a986089ee73111d5bfda48b0c7d29d3f9301571c",
    manifest="zerocopy",
    adapter_args=["--features", "derive,simd", "--xfail", "test_ui"],
    lockfile="$(S)/tst/projects/zerocopy/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

zerocopy_0_8_56 = add_project_test(
    name="zerocopy_0_8_56",
    url="https://github.com/google/zerocopy.git",
    rev="6dc429c451bdf1d7202ec1ec2cf426514e00d8eb",
    manifest="zerocopy",
    adapter_args=["--features", "derive,simd", "--xfail", "test_ui"],
    lockfile="$(S)/tst/projects/zerocopy_0_8_56/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

rustversion = add_project_test(
    name="rustversion",
    url="https://github.com/dtolnay/rustversion.git",
    rev="9e86f839b6a34a7d9398f243d88bf400b7fa1f7c",
    lockfile="$(S)/tst/projects/rustversion/Cargo.lock",
)

trybuild = add_project_test(
    name="trybuild",
    url="https://github.com/dtolnay/trybuild.git",
    rev="2adc26560dba1d8eaeb596c5625f854e5d6c68b2",
    lockfile="$(S)/tst/projects/trybuild/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

serde = add_project_test(
    name="serde",
    url="https://github.com/serde-rs/serde.git",
    rev="7fc3b4c30c94f73a96ebd1553f2b090d928fc3a8",
    manifest="serde",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/serde/Cargo.lock",
)

serde_derive_1_0_229 = add_project_test(
    name="serde_derive_1_0_229",
    url="https://github.com/serde-rs/serde.git",
    rev="7fc3b4c30c94f73a96ebd1553f2b090d928fc3a8",
    manifest="serde_derive",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/serde/Cargo.lock",
)

serde_core_1_0_229 = add_project_test(
    name="serde_core_1_0_229",
    url="https://github.com/serde-rs/serde.git",
    rev="7fc3b4c30c94f73a96ebd1553f2b090d928fc3a8",
    manifest="serde_core",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/serde/Cargo.lock",
)

elain = add_project_test(
    name="elain",
    url="https://github.com/jswrenn/elain.git",
    rev="a28dc120e15b915502241eab078984b1315eb9aa",
    lockfile="$(S)/tst/projects/elain/Cargo.lock",
)

zmij = add_project_test(
    name="zmij",
    url="https://github.com/dtolnay/zmij.git",
    rev="7b7cc48b58028e8af7be87e94c0c1c8936f1a57c",
    lockfile="$(S)/tst/projects/zmij/Cargo.lock",
)

num_bigint = add_project_test(
    name="num_bigint",
    url="https://github.com/rust-num/num-bigint.git",
    rev="33c59ba44b7bdb09975b38a321b1b88c6a444005",
    lockfile="$(S)/tst/projects/num-bigint/Cargo.lock",
)

# Sixteen crates added together on 2026-09-22 (lockfiles from cargo 1.90.0,
# sources pinned at the release tags). Their first run took 5m45s for all
# sixteen in parallel at -j 40, src and vendor nodes included. The three that
# passed - memchr, semver, pulldown-cmark - are budgeted at twice that bound
# by the tier rule; the thirteen red ones fail inside the compiler or a
# dependency before or during their tests, so their full duration is
# unmeasured and they keep the heavy budget until they run to the end.
hashbrown_0_17_1 = add_project_test(
    name="hashbrown_0_17_1",
    url="https://github.com/rust-lang/hashbrown.git",
    rev="c62a63a61b7caf2de8f9ecb7b06a66b0ab6bdf3d",
    lockfile="$(S)/tst/projects/hashbrown_0_17_1/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

indexmap_2_14_2 = add_project_test(
    name="indexmap_2_14_2",
    url="https://github.com/indexmap-rs/indexmap.git",
    rev="41a870887c4c77adf665886e63df08f406bfe37a",
    lockfile="$(S)/tst/projects/indexmap_2_14_2/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

memchr_2_8_3 = add_project_test(
    name="memchr_2_8_3",
    url="https://github.com/BurntSushi/memchr.git",
    rev="5fdb40c054e1fff359a2f7bdf7f87a13b34b465d",
    lockfile="$(S)/tst/projects/memchr_2_8_3/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

serde_json_1_0_151 = add_project_test(
    name="serde_json_1_0_151",
    url="https://github.com/serde-rs/json.git",
    rev="de8500740cdcabffb9734f503e4889def823cf10",
    lockfile="$(S)/tst/projects/serde_json_1_0_151/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

once_cell_1_21_4 = add_project_test(
    name="once_cell_1_21_4",
    url="https://github.com/matklad/once_cell.git",
    rev="80fe900b21f6d76c1a2ed74d3343e8a3a88c46d0",
    lockfile="$(S)/tst/projects/once_cell_1_21_4/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

smallvec_1_16_2 = add_project_test(
    name="smallvec_1_16_2",
    url="https://github.com/servo/rust-smallvec.git",
    rev="ccf5fc71044d491c46a3d79e7ed53948e6da1590",
    lockfile="$(S)/tst/projects/smallvec_1_16_2/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

regex_1_13_1 = add_project_test(
    name="regex_1_13_1",
    url="https://github.com/rust-lang/regex.git",
    rev="2b527599eb9eea0dcc288c704584f242f26a5c61",
    lockfile="$(S)/tst/projects/regex_1_13_1/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

aho_corasick_1_1_5 = add_project_test(
    name="aho_corasick_1_1_5",
    url="https://github.com/BurntSushi/aho-corasick.git",
    rev="5178060ce73d91938f8582d0360e3be031380440",
    lockfile="$(S)/tst/projects/aho_corasick_1_1_5/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

bytes_1_12_1 = add_project_test(
    name="bytes_1_12_1",
    url="https://github.com/tokio-rs/bytes.git",
    rev="76c0fbb54ed4336caf9d2311658a2f4a5627c21d",
    lockfile="$(S)/tst/projects/bytes_1_12_1/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

petgraph_0_8_3 = add_project_test(
    name="petgraph_0_8_3",
    url="https://github.com/petgraph/petgraph.git",
    rev="162903562ce5b00cdba390a0d9c1bb80f1c75bf5",
    lockfile="$(S)/tst/projects/petgraph_0_8_3/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

ryu_1_0_23 = add_project_test(
    name="ryu_1_0_23",
    url="https://github.com/dtolnay/ryu.git",
    rev="f0b52bb194befe6fd242154f2182fafd43a819b8",
    lockfile="$(S)/tst/projects/ryu_1_0_23/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

semver_1_0_28 = add_project_test(
    name="semver_1_0_28",
    url="https://github.com/dtolnay/semver.git",
    rev="7625c7aa3f0e8ba21e099d1765bcebcb72aa8816",
    lockfile="$(S)/tst/projects/semver_1_0_28/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

bumpalo_3_20_3 = add_project_test(
    name="bumpalo_3_20_3",
    url="https://github.com/fitzgen/bumpalo.git",
    rev="84654ace6be4444da3ff102a0a0af3b38c4df4fb",
    lockfile="$(S)/tst/projects/bumpalo_3_20_3/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

nom_8_0_0 = add_project_test(
    name="nom_8_0_0",
    url="https://github.com/rust-bakery/nom.git",
    rev="2cec1b3e4c9ccac62c902d60c00de6d1549ccbe1",
    lockfile="$(S)/tst/projects/nom_8_0_0/Cargo.lock",
    timeout=HEAVY_PROJECT_TIMEOUT,
)

sqlparser_0_63_0 = add_project_test(
    name="sqlparser_0_63_0",
    url="https://github.com/apache/datafusion-sqlparser-rs.git",
    rev="85b1a6f2223bf95d98cf1bc504971abd191dc869",
    lockfile="$(S)/tst/projects/sqlparser_0_63_0/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

pulldown_cmark_0_13_4 = add_project_test(
    name="pulldown_cmark_0_13_4",
    url="https://github.com/pulldown-cmark/pulldown-cmark.git",
    rev="38e4d08f14ec4bd9783270e9623db7681ebed968",
    manifest="pulldown-cmark",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/pulldown_cmark_0_13_4/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

tokio_1_53_1 = add_project_test(
    name="tokio_1_53_1",
    url="https://github.com/tokio-rs/tokio.git",
    rev="75fef53d0a8590c2d1dbb63672aa7b7d1ef51155",
    manifest="tokio",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/tokio_1_53_1/Cargo.lock",
    adapter_args=["--features", "full"],
    timeout=NESTED_PROJECT_TIMEOUT,
)

futures_0_3_34 = add_project_test(
    name="futures_0_3_34",
    url="https://github.com/rust-lang/futures-rs.git",
    rev="705e6b5c0f06535b1aac1cb1989a172b3d45be8c",
    manifest="futures",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/futures_0_3_34/Cargo.lock",
    # async_await_macros.rs's stream_select asserts that 999 draws from three
    # streams give each value at least 299 times. futures seeds each thread's
    # xorshift with SipHash (zero keys) of a process-wide counter, so the draws
    # depend only on how many threads asked for a seed first - which the other
    # tests of that binary race for. Seeds 0..200 on one thread each: 18, 56
    # and 58 fail, with the same counts under rustc 1.90 and ours; alone the
    # test always gets seed 0. The test calls itself "a bit flaky".
    adapter_args=[
        "--features", "default,thread-pool,io-compat",
        "--", "--skip", "stream_select", "--exact",
    ],
    timeout=NESTED_PROJECT_TIMEOUT,
)

quick_xml_0_42_0 = add_project_test(
    name="quick_xml_0_42_0",
    url="https://github.com/tafia/quick-xml.git",
    rev="36a2c52a4f6c90878c3f60c3c3b5c62efaef28a9",
    lockfile="$(S)/tst/projects/quick_xml_0_42_0/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

async_trait_0_1_92 = add_project_test(
    name="async_trait_0_1_92",
    url="https://github.com/dtolnay/async-trait.git",
    rev="82e7e9edd60f622294373a23c0ce9c0077ad0263",
    lockfile="$(S)/tst/projects/async_trait_0_1_92/Cargo.lock",
)

tracing_0_1_44 = add_project_test(
    name="tracing_0_1_44",
    url="https://github.com/tokio-rs/tracing.git",
    rev="2d55f6faf9be83e7e4634129fb96813241aac2b8",
    manifest="tracing",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/tracing_0_1_44/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

csv_1_4_0 = add_project_test(
    name="csv_1_4_0",
    url="https://github.com/BurntSushi/rust-csv.git",
    rev="4a3997e91d668ea1d8595bdef15625a77cf2308a",
    lockfile="$(S)/tst/projects/csv_1_4_0/Cargo.lock",
)

unicode_segmentation_1_13_13 = add_project_test(
    name="unicode_segmentation_1_13_13",
    url="https://github.com/unicode-rs/unicode-segmentation.git",
    rev="66a032fd8d667bc47ac5b640b151dff3f5356d07",
    lockfile="$(S)/tst/projects/unicode_segmentation_1_13_13/Cargo.lock",
)

encoding_rs_0_8_41 = add_project_test(
    name="encoding_rs_0_8_41",
    url="https://github.com/hsivonen/encoding_rs.git",
    rev="08604915e6f1a3b7a93398aec0bb92d7a6611a6a",
    lockfile="$(S)/tst/projects/encoding_rs_0_8_41/Cargo.lock",
)

toml_1_1_6 = add_project_test(
    name="toml_1_1_6",
    url="https://github.com/toml-rs/toml.git",
    rev="572c005d80cca5f7bd163805c2f33ba0a5207b6d",
    manifest="crates/toml",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/toml_1_1_6/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

rand_0_9_5 = add_project_test(
    name="rand_0_9_5",
    url="https://github.com/rust-random/rand.git",
    rev="3474ec047279e5de32676c50154ff7d76d12a94e",
    lockfile="$(S)/tst/projects/rand_0_9_5/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

chrono_0_4_45 = add_project_test(
    name="chrono_0_4_45",
    url="https://github.com/chronotope/chrono.git",
    rev="170338250e836976a211e64728ec956e45e78a39",
    lockfile="$(S)/tst/projects/chrono_0_4_45/Cargo.lock",
    adapter_args=["--xfail", "gen_bindings"],
    timeout=NESTED_PROJECT_TIMEOUT,
)

url_2_5_8 = add_project_test(
    name="url_2_5_8",
    url="https://github.com/servo/rust-url.git",
    rev="d6ea13c5f8e7e6e627f6390161b3e185bda5e5ce",
    manifest="url",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/url_2_5_8/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

uuid_1_26_1 = add_project_test(
    name="uuid_1_26_1",
    url="https://github.com/uuid-rs/uuid.git",
    rev="9f927126c89892ddfed6cd2f92df16852f3f9aa6",
    lockfile="$(S)/tst/projects/uuid_1_26_1/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

anyhow_1_0_104 = add_project_test(
    name="anyhow_1_0_104",
    url="https://github.com/dtolnay/anyhow.git",
    rev="1dbe1862aae650423e3361fbd20b7d17c5109cc3",
    lockfile="$(S)/tst/projects/anyhow_1_0_104/Cargo.lock",
)

thiserror_2_0_20 = add_project_test(
    name="thiserror_2_0_20",
    url="https://github.com/dtolnay/thiserror.git",
    rev="b1d5db5e039275d95bf7536a2b2192aeb4dc28bf",
    lockfile="$(S)/tst/projects/thiserror_2_0_20/Cargo.lock",
)

arrayvec_0_7_8 = add_project_test(
    name="arrayvec_0_7_8",
    url="https://github.com/bluss/arrayvec.git",
    rev="0cb664cf505844348538230479b0040b44f3faf1",
    lockfile="$(S)/tst/projects/arrayvec_0_7_8/Cargo.lock",
)

crossbeam_channel_0_5_17 = add_project_test(
    name="crossbeam_channel_0_5_17",
    url="https://github.com/crossbeam-rs/crossbeam.git",
    rev="2920c984290229ab4e0ca0452ef09e48a82063f3",
    manifest="crossbeam-channel",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/crossbeam_channel_0_5_17/Cargo.lock",
    # golang.rs's select2 counts the whole process's net allocations through a
    # counting #[global_allocator] and requires the second round to grow them
    # by at most N + 10000 bytes. The 25 other tests of that binary run on
    # other threads meanwhile and allocate through the same counter, so the
    # bound holds only when none of them is mid-allocation across the window:
    # measured 5/5 alone (`select2::main --exact`), 3/5 with the rest.
    adapter_args=["--", "--skip", "select2::main"],
    timeout=NESTED_PROJECT_TIMEOUT,
)

parking_lot_0_12_5 = add_project_test(
    name="parking_lot_0_12_5",
    url="https://github.com/Amanieu/parking_lot.git",
    rev="d7828fff7b5d6327ae608e82db45f888b344449a",
    lockfile="$(S)/tst/projects/parking_lot_0_12_5/Cargo.lock",
)

strum_0_28_0 = add_project_test(
    name="strum_0_28_0",
    url="https://github.com/Peternator7/strum.git",
    rev="7376771128834d28bb9beba5c39846cba62e71ec",
    manifest="strum_tests",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/strum_0_28_0/Cargo.lock",
    timeout=NESTED_PROJECT_TIMEOUT,
)

time_0_3_55 = add_project_test(
    name="time_0_3_55",
    url="https://github.com/time-rs/time.git",
    rev="857d9c404c5b6f6cb64d1bfa604c695be3369e12",
    manifest="time",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/time_0_3_55/Cargo.lock",
    adapter_args=["--all-features"],
    timeout=NESTED_PROJECT_TIMEOUT,
)

proc_macro2_1_0_107 = add_project_test(
    name="proc_macro2_1_0_107",
    url="https://github.com/dtolnay/proc-macro2.git",
    rev="ed8a5497669cd63db33bf24646f261b012bbbc4a",
    lockfile="$(S)/tst/projects/proc_macro2_1_0_107/Cargo.lock",
)

quote_1_0_47 = add_project_test(
    name="quote_1_0_47",
    url="https://github.com/dtolnay/quote.git",
    rev="723dcb47d3f0ddc896e17287c8a8d3f2ea2317d5",
    lockfile="$(S)/tst/projects/quote_1_0_47/Cargo.lock",
)

log_0_4_34 = add_project_test(
    name="log_0_4_34",
    url="https://github.com/rust-lang/log.git",
    rev="8034743dd9d7f7583bd9a670271483d176130911",
    lockfile="$(S)/tst/projects/log_0_4_34/Cargo.lock",
)

either_1_18_0 = add_project_test(
    name="either_1_18_0",
    url="https://github.com/rayon-rs/either.git",
    rev="ce6f07fc3d56d6a56ecfc32256d2c33bd0b4ad09",
    lockfile="$(S)/tst/projects/either_1_18_0/Cargo.lock",
)

num_traits_0_2_19 = add_project_test(
    name="num_traits_0_2_19",
    url="https://github.com/rust-num/num-traits.git",
    rev="7ec3d41d39b28190ec1d42db38021107b3951f3a",
    lockfile="$(S)/tst/projects/num_traits_0_2_19/Cargo.lock",
)

byteorder_1_5_0 = add_project_test(
    name="byteorder_1_5_0",
    url="https://github.com/BurntSushi/byteorder.git",
    rev="ec068eefa042d494475db125c4b034bd8e9e34dd",
    lockfile="$(S)/tst/projects/byteorder_1_5_0/Cargo.lock",
)

hex_0_4_3 = add_project_test(
    name="hex_0_4_3",
    url="https://github.com/KokaKiwi/rust-hex.git",
    rev="b2b4370b5bf021b98ee7adc92233e8de3f2de792",
    lockfile="$(S)/tst/projects/hex_0_4_3/Cargo.lock",
)

sha2_0_10_9 = add_project_test(
    name="sha2_0_10_9",
    url="https://github.com/RustCrypto/hashes.git",
    rev="82c36a428f8d6f05f3bfccdedb243e9d1f85359d",
    manifest="sha2",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/sha2_0_10_9/Cargo.lock",
)

crc32fast_1_5_2 = add_project_test(
    name="crc32fast_1_5_2",
    url="https://github.com/srijs/rust-crc32fast.git",
    rev="7eb0b8a2c9b246d27dc4cb5a53d3fc3b43a4bb83",
    lockfile="$(S)/tst/projects/crc32fast_1_5_2/Cargo.lock",
)

flate2_1_1_10 = add_project_test(
    name="flate2_1_1_10",
    url="https://github.com/rust-lang/flate2-rs.git",
    rev="ed93d4fc60eaf876c6aded741bf992d524551930",
    lockfile="$(S)/tst/projects/flate2_1_1_10/Cargo.lock",
)

tempfile_3_27_0 = add_project_test(
    name="tempfile_3_27_0",
    url="https://github.com/Stebalien/tempfile.git",
    rev="5c8fa12eb584931b4f1bccfde87eb72fbfa7dc61",
    lockfile="$(S)/tst/projects/tempfile_3_27_0/Cargo.lock",
)

walkdir_2_5_0 = add_project_test(
    name="walkdir_2_5_0",
    url="https://github.com/BurntSushi/walkdir.git",
    rev="4f26be4d450910916ea11533b2efc52b9a6483bc",
    lockfile="$(S)/tst/projects/walkdir_2_5_0/Cargo.lock",
)

bstr_1_13_1 = add_project_test(
    name="bstr_1_13_1",
    url="https://github.com/BurntSushi/bstr.git",
    rev="134195be38c7c9a4887980fd0f6f8bae042dd91e",
    lockfile="$(S)/tst/projects/bstr_1_13_1/Cargo.lock",
)

unicode_width_0_2_2 = add_project_test(
    name="unicode_width_0_2_2",
    url="https://github.com/unicode-rs/unicode-width.git",
    rev="9d98411769fe13c7c18cab0b3fbbab29ba8350ea",
    lockfile="$(S)/tst/projects/unicode_width_0_2_2/Cargo.lock",
)

dashmap_6_2_1 = add_project_test(
    name="dashmap_6_2_1",
    url="https://github.com/xacrimon/dashmap.git",
    rev="749ed1f965115e9e1920d2fc7ae65f633858b021",
    lockfile="$(S)/tst/projects/dashmap_6_2_1/Cargo.lock",
)

slab_0_4_12 = add_project_test(
    name="slab_0_4_12",
    url="https://github.com/tokio-rs/slab.git",
    rev="a1e4346070a48c936d808de75191dee5d01e433c",
    lockfile="$(S)/tst/projects/slab_0_4_12/Cargo.lock",
)

winnow_0_7_15 = add_project_test(
    name="winnow_0_7_15",
    url="https://github.com/winnow-rs/winnow.git",
    rev="eae4d4a23c400fec27a01cfb7115bc7808374f40",
    lockfile="$(S)/tst/projects/winnow_0_7_15/Cargo.lock",
)

http_1_5_0 = add_project_test(
    name="http_1_5_0",
    url="https://github.com/hyperium/http.git",
    rev="16fc9a7b840c2181e7f8b37397c107b0ffcd050d",
    lockfile="$(S)/tst/projects/http_1_5_0/Cargo.lock",
)

httparse_1_10_1 = add_project_test(
    name="httparse_1_10_1",
    url="https://github.com/seanmonstar/httparse.git",
    rev="9f29e79f9832dbd0ae5220acb17c1866745bdecd",
    lockfile="$(S)/tst/projects/httparse_1_10_1/Cargo.lock",
)

unicode_normalization_0_1_24 = add_project_test(
    name="unicode_normalization_0_1_24",
    url="https://github.com/unicode-rs/unicode-normalization.git",
    rev="c9921309f09ebd05108920fda92efbf5f8124a7d",
    lockfile="$(S)/tst/projects/unicode_normalization_0_1_24/Cargo.lock",
)

itoa_1_0_18 = add_project_test(
    name="itoa_1_0_18",
    url="https://github.com/dtolnay/itoa.git",
    rev="af77385d0daf4d0e949e81f2588be2e44f69f086",
    lockfile="$(S)/tst/projects/itoa_1_0_18/Cargo.lock",
)


fnv_1_0_7 = add_project_test(
    name="fnv_1_0_7",
    url="https://github.com/servo/rust-fnv.git",
    rev="4b4784ebfd3332dc61f0640764d6f1140e03a9ab",
    lockfile="$(S)/tst/projects/fnv_1_0_7/Cargo.lock",
)

rustc_hash_2_1_3 = add_project_test(
    name="rustc_hash_2_1_3",
    url="https://github.com/rust-lang/rustc-hash.git",
    rev="c13e7ccca705e6255387a2ebc6dca142d6881621",
    lockfile="$(S)/tst/projects/rustc_hash_2_1_3/Cargo.lock",
)

ahash_0_8_12 = add_project_test(
    name="ahash_0_8_12",
    url="https://github.com/tkaitchuck/aHash.git",
    rev="10c4f487e85c62bb12618ab5a4bb84b16802cdad",
    lockfile="$(S)/tst/projects/ahash_0_8_12/Cargo.lock",
    # tests/nopanic.rs is no-panic's check: each `#[no_panic]` function
    # guards its body with a value whose drop calls an extern symbol that
    # does not exist, so it links only once the optimizer has proven the
    # body cannot unwind and deleted the drop. The crate asks for that
    # proof from opt-level 2 with fat LTO; our backend has no LTO, so the
    # calls into ahash's rlib stay opaque, may unwind, and the link fails.
    adapter_args=["--xfail-target", "nopanic"],
    # ahash's [profile.test] asks for opt-level 2, which our cargo honours, so
    # its whole dev-dependency graph (criterion among it) goes through the C++
    # compiler at -O2, and the nopanic target is built a second time by its
    # own expected-failure run. 3m53s alone at a load average of 3 - most of
    # the five-minute budget - and killed at five minutes in three full
    # corpus runs at load averages between 20 and 40.
    timeout=NESTED_PROJECT_TIMEOUT,
)

humantime_2_4_0 = add_project_test(
    name="humantime_2_4_0",
    url="https://github.com/chronotope/humantime.git",
    rev="fc092817fa8689298eaac28ff49bd8bede4ff605",
    lockfile="$(S)/tst/projects/humantime_2_4_0/Cargo.lock",
)

glob_0_3_4 = add_project_test(
    name="glob_0_3_4",
    url="https://github.com/rust-lang/glob.git",
    rev="cfa2a58f2e44373573f657ec25b3621e44714dee",
    lockfile="$(S)/tst/projects/glob_0_3_4/Cargo.lock",
)

textwrap_0_16_4 = add_project_test(
    name="textwrap_0_16_4",
    url="https://github.com/mgeisler/textwrap.git",
    rev="5246c6367058468f5813abb47a48dcc138e2a5d5",
    lockfile="$(S)/tst/projects/textwrap_0_16_4/Cargo.lock",
)

strsim_0_11_1 = add_project_test(
    name="strsim_0_11_1",
    url="https://github.com/rapidfuzz/strsim-rs.git",
    rev="f72cd1cbbfc0b43db217a9c57c543b025bdba863",
    lockfile="$(S)/tst/projects/strsim_0_11_1/Cargo.lock",
)

env_logger_0_11_11 = add_project_test(
    name="env_logger_0_11_11",
    url="https://github.com/rust-cli/env_logger.git",
    rev="b4d3f2b8dd3f1c3362f07da8f6f4a30c701358cf",
    lockfile="$(S)/tst/projects/env_logger_0_11_11/Cargo.lock",
)

ordered_float_5_5_0 = add_project_test(
    name="ordered_float_5_5_0",
    url="https://github.com/reem/rust-ordered-float.git",
    rev="2d56f3e8ab8bfea8d67df7d7295791ccad102f66",
    lockfile="$(S)/tst/projects/ordered_float_5_5_0/Cargo.lock",
)

half_2_7_1 = add_project_test(
    name="half_2_7_1",
    url="https://github.com/VoidStarKat/half-rs.git",
    rev="8cc891f3e4aad956eca7fa79b1f42f87ecd141ae",
    lockfile="$(S)/tst/projects/half_2_7_1/Cargo.lock",
)

bytemuck_1_25_2 = add_project_test(
    name="bytemuck_1_25_2",
    url="https://github.com/Lokathor/bytemuck.git",
    rev="f363643e951a7ac9e4b9921de982f2b0918902e3",
    lockfile="$(S)/tst/projects/bytemuck_1_25_2/Cargo.lock",
)

tinyvec_1_13_3 = add_project_test(
    name="tinyvec_1_13_3",
    url="https://github.com/Lokathor/tinyvec.git",
    rev="e343bbbb5a6594c9d57de718e6f232e6c0115dcb",
    lockfile="$(S)/tst/projects/tinyvec_1_13_3/Cargo.lock",
)

paste_1_0_15 = add_project_test(
    name="paste_1_0_15",
    url="https://github.com/dtolnay/paste.git",
    rev="a2c7e27875277450ed28147623ba5218dd23e732",
    lockfile="$(S)/tst/projects/paste_1_0_15/Cargo.lock",
)

indoc_2_0_7 = add_project_test(
    name="indoc_2_0_7",
    url="https://github.com/dtolnay/indoc.git",
    rev="8d78216b3f127f523d198475ea44090f8f6894d5",
    lockfile="$(S)/tst/projects/indoc_2_0_7/Cargo.lock",
)


num_integer_0_1_47 = add_project_test(
    name="num_integer_0_1_47",
    url="https://github.com/rust-num/num-integer.git",
    rev="765fd9b3eb9397471d41b9f613e530f6d9d6383f",
    lockfile="$(S)/tst/projects/num_integer_0_1_47/Cargo.lock",
)

fixedbitset_0_5_7 = add_project_test(
    name="fixedbitset_0_5_7",
    url="https://github.com/petgraph/fixedbitset.git",
    rev="f1db5d17dabc4b8f3ba68c1228a3ee7601c7f33c",
    lockfile="$(S)/tst/projects/fixedbitset_0_5_7/Cargo.lock",
)

scopeguard_1_2_0 = add_project_test(
    name="scopeguard_1_2_0",
    url="https://github.com/bluss/scopeguard.git",
    rev="bb988222848d39b0b9037b29da3a3a1eb05ebf4b",
    lockfile="$(S)/tst/projects/scopeguard_1_2_0/Cargo.lock",
)

similar_2_7_0 = add_project_test(
    name="similar_2_7_0",
    url="https://github.com/mitsuhiko/similar.git",
    rev="28c146b628119065e9a4dae569eaa570a4632c17",
    lockfile="$(S)/tst/projects/similar_2_7_0/Cargo.lock",
)

lru_0_18_5 = add_project_test(
    name="lru_0_18_5",
    url="https://github.com/jeromefroe/lru-rs.git",
    rev="f1e972197053a6814e77b77afacb51f03fc03170",
    lockfile="$(S)/tst/projects/lru_0_18_5/Cargo.lock",
)

adler2_2_0_1 = add_project_test(
    name="adler2_2_0_1",
    url="https://github.com/oyvindln/adler2.git",
    rev="89a031a0f42eeff31c70dc598b398cbf31f1680f",
    lockfile="$(S)/tst/projects/adler2_2_0_1/Cargo.lock",
)

anstyle_1_0_11 = add_project_test(
    name="anstyle_1_0_11",
    url="https://github.com/rust-cli/anstyle.git",
    rev="886539c95318db5de9db49b6d66d19413bd308cc",
    manifest="crates/anstyle",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/anstyle_1_0_11/Cargo.lock",
)

arrayref_0_3_9 = add_project_test(
    name="arrayref_0_3_9",
    url="https://github.com/droundy/arrayref.git",
    rev="f8d0299d863922db6c409d08098941e833b70d69",
    lockfile="$(S)/tst/projects/arrayref_0_3_9/Cargo.lock",
)

autocfg_1_5_0 = add_project_test(
    name="autocfg_1_5_0",
    url="https://github.com/cuviper/autocfg.git",
    rev="d912169ed67977efe5a465269b0e73cb66060c49",
    lockfile="$(S)/tst/projects/autocfg_1_5_0/Cargo.lock",
)

blake3_1_8_2 = add_project_test(
    name="blake3_1_8_2",
    url="https://github.com/BLAKE3-team/BLAKE3.git",
    rev="df610ddc3b93841ffc59a87e3da659a15910eb46",
    lockfile="$(S)/tst/projects/blake3_1_8_2/Cargo.lock",
)

block_buffer_0_10_4 = add_project_test(
    name="block_buffer_0_10_4",
    url="https://github.com/RustCrypto/utils.git",
    rev="6d35952d3d3b124bc1049ad6fb406b42b1ce4bfe",
    manifest="block-buffer",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/block_buffer_0_10_4/Cargo.lock",
)

cc_1_2_16 = add_project_test(
    name="cc_1_2_16",
    url="https://github.com/rust-lang/cc-rs.git",
    rev="3c1325b09a78827fb2beb3ea9e8f1e3f84876b64",
    lockfile="$(S)/tst/projects/cc_1_2_16/Cargo.lock",
)

cfg_aliases_0_2_1 = add_project_test(
    name="cfg_aliases_0_2_1",
    url="https://github.com/katharostech/cfg_aliases.git",
    rev="3d55ba79872b61265a7176110a5200df0c9d9e54",
    lockfile="$(S)/tst/projects/cfg_aliases_0_2_1/Cargo.lock",
)

cfg_if_1_0_1 = add_project_test(
    name="cfg_if_1_0_1",
    url="https://github.com/rust-lang/cfg-if.git",
    rev="dbfd66354537a7d47d84c95ea28b9a6f169ba9d1",
    lockfile="$(S)/tst/projects/cfg_if_1_0_1/Cargo.lock",
)

constant_time_eq_0_3_1 = add_project_test(
    name="constant_time_eq_0_3_1",
    url="https://github.com/cesarb/constant_time_eq.git",
    rev="bea93a336b65dee32f1ed871ea1eecdf47977bb2",
    lockfile="$(S)/tst/projects/constant_time_eq_0_3_1/Cargo.lock",
)

cpufeatures_0_2_17 = add_project_test(
    name="cpufeatures_0_2_17",
    url="https://github.com/RustCrypto/utils.git",
    rev="9d92d5e95ab4c07c5d8bfd024bf2a17e96d20feb",
    manifest="cpufeatures",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/cpufeatures_0_2_17/Cargo.lock",
)

crossbeam_deque_0_8_6 = add_project_test(
    name="crossbeam_deque_0_8_6",
    url="https://github.com/crossbeam-rs/crossbeam.git",
    rev="ccd83ac4108a2a1b41e9c6e79c87267167d18dfa",
    manifest="crossbeam-deque",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/crossbeam_deque_0_8_6/Cargo.lock",
)

crossbeam_epoch_0_9_18 = add_project_test(
    name="crossbeam_epoch_0_9_18",
    url="https://github.com/crossbeam-rs/crossbeam.git",
    rev="9c3182abebb36bdc9446d75d4644190fef70fa01",
    manifest="crossbeam-epoch",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/crossbeam_epoch_0_9_18/Cargo.lock",
)

crypto_common_0_1_6 = add_project_test(
    name="crypto_common_0_1_6",
    url="https://github.com/RustCrypto/traits.git",
    rev="25614e2d5a4ccbb0cfde23367a93c8bcdbfe421a",
    manifest="crypto-common",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/crypto_common_0_1_6/Cargo.lock",
)

ctrlc_3_4_7 = add_project_test(
    name="ctrlc_3_4_7",
    url="https://github.com/Detegr/rust-ctrlc.git",
    rev="ac79af3262199bce508257771b61608df4db22c4",
    lockfile="$(S)/tst/projects/ctrlc_3_4_7/Cargo.lock",
)

darling_0_20_11 = add_project_test(
    name="darling_0_20_11",
    url="https://github.com/TedDriggs/darling.git",
    rev="82a51e0b65b158de02ffb5d753ea4cd03529743b",
    lockfile="$(S)/tst/projects/darling_0_20_11/Cargo.lock",
)

darling_core_0_20_11 = add_project_test(
    name="darling_core_0_20_11",
    url="https://github.com/TedDriggs/darling.git",
    rev="82a51e0b65b158de02ffb5d753ea4cd03529743b",
    manifest="core",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/darling_core_0_20_11/Cargo.lock",
)

darling_macro_0_20_11 = add_project_test(
    name="darling_macro_0_20_11",
    url="https://github.com/TedDriggs/darling.git",
    rev="82a51e0b65b158de02ffb5d753ea4cd03529743b",
    manifest="macro",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/darling_macro_0_20_11/Cargo.lock",
)

datafrog_2_0_1 = add_project_test(
    name="datafrog_2_0_1",
    url="https://github.com/rust-lang-nursery/datafrog.git",
    rev="9cc6b5d8e29037dc1a199420d63708b734b6cc6e",
    lockfile="$(S)/tst/projects/datafrog_2_0_1/Cargo.lock",
)

derive_setters_0_1_8 = add_project_test(
    name="derive_setters_0_1_8",
    url="https://github.com/Lymia/derive_setters.git",
    rev="7398bc189ac22b1bca214445a74949fe051df34a",
    lockfile="$(S)/tst/projects/derive_setters_0_1_8/Cargo.lock",
)

derive_where_1_5_0 = add_project_test(
    name="derive_where_1_5_0",
    url="https://github.com/ModProg/derive-where.git",
    rev="bcd9dcc7bc6db6e4f8803671b01bfa0cec8a2fe9",
    lockfile="$(S)/tst/projects/derive_where_1_5_0/Cargo.lock",
)

digest_0_10_7 = add_project_test(
    name="digest_0_10_7",
    url="https://github.com/RustCrypto/traits.git",
    rev="344389411fd9718a0742435152e933a9e71461ee",
    manifest="digest",
    lockfile="$(S)/tst/projects/digest_0_10_7/Cargo.lock",
)

displaydoc_0_2_5 = add_project_test(
    name="displaydoc_0_2_5",
    url="https://github.com/yaahc/displaydoc.git",
    rev="e4028851bfb82998300237f7568a45f589a19e40",
    lockfile="$(S)/tst/projects/displaydoc_0_2_5/Cargo.lock",
)

ena_0_14_3 = add_project_test(
    name="ena_0_14_3",
    url="https://github.com/rust-lang/ena.git",
    rev="8e88541a49fa248e15aeb138e404ac5415e862ed",
    lockfile="$(S)/tst/projects/ena_0_14_3/Cargo.lock",
)

equivalent_1_0_2 = add_project_test(
    name="equivalent_1_0_2",
    url="https://github.com/indexmap-rs/equivalent.git",
    rev="44cdd44f8b8ebb5f9ae096c7550a5e74ffb7d6ae",
    lockfile="$(S)/tst/projects/equivalent_1_0_2/Cargo.lock",
)

fallible_iterator_0_3_0 = add_project_test(
    name="fallible_iterator_0_3_0",
    url="https://github.com/sfackler/rust-fallible-iterator.git",
    rev="11cfa0558045e52bdb473c9eab6a1eb35a54e302",
    lockfile="$(S)/tst/projects/fallible_iterator_0_3_0/Cargo.lock",
)

fastrand_2_3_0 = add_project_test(
    name="fastrand_2_3_0",
    url="https://github.com/smol-rs/fastrand.git",
    rev="8419f8916f08c63572b4f7cbdc07cec94c1fc5ed",
    lockfile="$(S)/tst/projects/fastrand_2_3_0/Cargo.lock",
)

fluent_bundle_0_16_0 = add_project_test(
    name="fluent_bundle_0_16_0",
    url="https://github.com/projectfluent/fluent-rs.git",
    rev="f22da4ea48328b4c617b7666c482634c49fbe0a7",
    manifest="fluent-bundle",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/fluent_bundle_0_16_0/Cargo.lock",
)

fluent_langneg_0_13_0 = add_project_test(
    name="fluent_langneg_0_13_0",
    url="https://github.com/projectfluent/fluent-langneg-rs.git",
    rev="29007f8d453b931b79544566b1037865bfa7fb12",
    lockfile="$(S)/tst/projects/fluent_langneg_0_13_0/Cargo.lock",
)

fluent_syntax_0_12_0 = add_project_test(
    name="fluent_syntax_0_12_0",
    url="https://github.com/projectfluent/fluent-rs.git",
    rev="f22da4ea48328b4c617b7666c482634c49fbe0a7",
    manifest="fluent-syntax",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/fluent_syntax_0_12_0/Cargo.lock",
)

foldhash_0_1_5 = add_project_test(
    name="foldhash_0_1_5",
    url="https://github.com/orlp/foldhash.git",
    rev="42461756c3760f9165c90d9626a49e36980a1e47",
    lockfile="$(S)/tst/projects/foldhash_0_1_5/Cargo.lock",
)

getopts_0_2_23 = add_project_test(
    name="getopts_0_2_23",
    url="https://github.com/rust-lang/getopts.git",
    rev="57b183a98599261ab535401b64aa0a46ab0b6a44",
    lockfile="$(S)/tst/projects/getopts_0_2_23/Cargo.lock",
)

getrandom_0_3_3 = add_project_test(
    name="getrandom_0_3_3",
    url="https://github.com/rust-random/getrandom.git",
    rev="82396406b28f23ba86e3e511d34a4f5dab0fda08",
    lockfile="$(S)/tst/projects/getrandom_0_3_3/Cargo.lock",
)

gsgdt_0_1_2 = add_project_test(
    name="gsgdt_0_1_2",
    url="https://github.com/vn-ki/gsgdt-rs.git",
    rev="96c79219f7bad57bfd951fceea92cb549e7f2ca4",
    lockfile="$(S)/tst/projects/gsgdt_0_1_2/Cargo.lock",
)

icu_list_data_1_5_1 = add_project_test(
    name="icu_list_data_1_5_1",
    url="https://github.com/unicode-org/icu4x.git",
    rev="044c9659f217ad1e197fb778701829e4f597646c",
    manifest="provider/baked/list",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/icu_list_data_1_5_1/Cargo.lock",
)

icu_locid_1_5_0 = add_project_test(
    name="icu_locid_1_5_0",
    url="https://github.com/unicode-org/icu4x.git",
    rev="55cd12ebb25c6261492e1e3dfa2e6453c54dde31",
    manifest="components/locid",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/icu_locid_1_5_0/Cargo.lock",
)

icu_locid_transform_data_1_5_1 = add_project_test(
    name="icu_locid_transform_data_1_5_1",
    url="https://github.com/unicode-org/icu4x.git",
    rev="044c9659f217ad1e197fb778701829e4f597646c",
    manifest="provider/baked/locid_transform",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/icu_locid_transform_data_1_5_1/Cargo.lock",
)

ident_case_1_0_1 = add_project_test(
    name="ident_case_1_0_1",
    url="https://github.com/TedDriggs/ident_case.git",
    rev="bf0d863e3006b40a0d923a81d7b2dd2db1136c2c",
    lockfile="$(S)/tst/projects/ident_case_1_0_1/Cargo.lock",
)

intl_memoizer_0_5_3 = add_project_test(
    name="intl_memoizer_0_5_3",
    url="https://github.com/projectfluent/fluent-rs.git",
    rev="f22da4ea48328b4c617b7666c482634c49fbe0a7",
    manifest="intl-memoizer",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/intl_memoizer_0_5_3/Cargo.lock",
)

intl_pluralrules_7_0_2 = add_project_test(
    name="intl_pluralrules_7_0_2",
    url="https://github.com/zbraniecki/pluralrules.git",
    rev="d0715d5b7cca72e228d6e6c4de154bd30dfdd06a",
    manifest="intl_pluralrules",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/intl_pluralrules_7_0_2/Cargo.lock",
)

jobserver_0_1_33 = add_project_test(
    name="jobserver_0_1_33",
    url="https://github.com/rust-lang/jobserver-rs.git",
    rev="65921f1d58ebb4a2511d45aadcc31d586042d73c",
    lockfile="$(S)/tst/projects/jobserver_0_1_33/Cargo.lock",
)

leb128_0_2_5 = add_project_test(
    name="leb128_0_2_5",
    url="https://github.com/gimli-rs/leb128.git",
    rev="34ce0fce88281a6c7e7bf4e51505ea29ef2b712f",
    lockfile="$(S)/tst/projects/leb128_0_2_5/Cargo.lock",
)

libc_0_2_174 = add_project_test(
    name="libc_0_2_174",
    url="https://github.com/rust-lang/libc.git",
    rev="ea6f07f9828c007a752fab78eedc0565f36096df",
    lockfile="$(S)/tst/projects/libc_0_2_174/Cargo.lock",
)

libloading_0_8_8 = add_project_test(
    name="libloading_0_8_8",
    url="https://github.com/nagisa/rust_libloading.git",
    rev="83f08b8779f4ba41777c41218398df4a2977d340",
    lockfile="$(S)/tst/projects/libloading_0_8_8/Cargo.lock",
)

linux_raw_sys_0_9_4 = add_project_test(
    name="linux_raw_sys_0_9_4",
    url="https://github.com/sunfishcode/linux-raw-sys.git",
    rev="d7d733c04380b4f15e97806b61da254c4e649887",
    lockfile="$(S)/tst/projects/linux_raw_sys_0_9_4/Cargo.lock",
)

lock_api_0_4_13 = add_project_test(
    name="lock_api_0_4_13",
    url="https://github.com/Amanieu/parking_lot.git",
    rev="df66e66b99f2650043b588cb0172b40958bc4277",
    manifest="lock_api",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/lock_api_0_4_13/Cargo.lock",
)

matchers_0_1_0 = add_project_test(
    name="matchers_0_1_0",
    url="https://github.com/hawkw/matchers.git",
    rev="6e5f38da23303a0c66133c9c2981818b93ab746d",
    lockfile="$(S)/tst/projects/matchers_0_1_0/Cargo.lock",
)

md_5_0_10_6 = add_project_test(
    name="md_5_0_10_6",
    url="https://github.com/RustCrypto/hashes.git",
    rev="026b0e81e90ba476a5d343a8e4efbc63b814737b",
    manifest="md5",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/md_5_0_10_6/Cargo.lock",
)

measureme_12_0_3 = add_project_test(
    name="measureme_12_0_3",
    url="https://github.com/rust-lang/measureme.git",
    rev="5ac839c602b59eee9c908b3b35b6d6c0cd1c42f7",
    manifest="measureme",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/measureme_12_0_3/Cargo.lock",
)

memmap2_0_2_3 = add_project_test(
    name="memmap2_0_2_3",
    url="https://github.com/RazrFalcon/memmap2-rs.git",
    rev="d5ed1da121a55a571793499fa2df25a970eb2b4f",
    lockfile="$(S)/tst/projects/memmap2_0_2_3/Cargo.lock",
)

miniz_oxide_0_8_9 = add_project_test(
    name="miniz_oxide_0_8_9",
    url="https://github.com/Frommi/miniz_oxide.git",
    rev="44e43c7786e379b2b1a7fde4aa0e63be719e583d",
    manifest="miniz_oxide",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/miniz_oxide_0_8_9/Cargo.lock",
)

nu_ansi_term_0_50_1 = add_project_test(
    name="nu_ansi_term_0_50_1",
    url="https://github.com/nushell/nu-ansi-term.git",
    rev="0912f8f6a3b29a8409cf5cab25e8865f5766ea96",
    lockfile="$(S)/tst/projects/nu_ansi_term_0_50_1/Cargo.lock",
)

odht_0_3_1 = add_project_test(
    name="odht_0_3_1",
    url="https://github.com/rust-lang/odht.git",
    rev="bc31b05fe859f4390f4cb76a90673e2e782c94a4",
    lockfile="$(S)/tst/projects/odht_0_3_1/Cargo.lock",
)

overload_0_1_1 = add_project_test(
    name="overload_0_1_1",
    url="https://github.com/danaugrs/overload.git",
    rev="4a8ec5b58afc53ea04c3a0f9b2f7a2c42208678c",
    lockfile="$(S)/tst/projects/overload_0_1_1/Cargo.lock",
)

parking_lot_core_0_9_11 = add_project_test(
    name="parking_lot_core_0_9_11",
    url="https://github.com/Amanieu/parking_lot.git",
    rev="df66e66b99f2650043b588cb0172b40958bc4277",
    manifest="core",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/parking_lot_core_0_9_11/Cargo.lock",
)

pathdiff_0_2_3 = add_project_test(
    name="pathdiff_0_2_3",
    url="https://github.com/Manishearth/pathdiff.git",
    rev="5180ff5b23d9d7eef0a14de13a3d814eb5d8d65c",
    lockfile="$(S)/tst/projects/pathdiff_0_2_3/Cargo.lock",
)

pin_project_lite_0_2_16 = add_project_test(
    name="pin_project_lite_0_2_16",
    url="https://github.com/taiki-e/pin-project-lite.git",
    rev="cca1e8ae094ceff53e74abbfec8c9f2221ebd202",
    lockfile="$(S)/tst/projects/pin_project_lite_0_2_16/Cargo.lock",
)

polonius_engine_0_13_0 = add_project_test(
    name="polonius_engine_0_13_0",
    url="https://github.com/rust-lang-nursery/polonius.git",
    rev="741e6095fe50f468982d4d253e842eb7d12ed9d3",
    lockfile="$(S)/tst/projects/polonius_engine_0_13_0/Cargo.lock",
)

ppv_lite86_0_2_21 = add_project_test(
    name="ppv_lite86_0_2_21",
    url="https://github.com/cryptocorrosion/cryptocorrosion.git",
    rev="000a6cd6bbcb0b091381dc5f8fe6d6efa480b818",
    manifest="utils-simd/ppv-lite86",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/ppv_lite86_0_2_21/Cargo.lock",
)

proc_macro_hack_0_5_20 = add_project_test(
    name="proc_macro_hack_0_5_20",
    url="https://github.com/dtolnay/proc-macro-hack.git",
    rev="fe0889cb7a67adfe070fad449066034e7a662f56",
    lockfile="$(S)/tst/projects/proc_macro_hack_0_5_20/Cargo.lock",
)

psm_0_1_26 = add_project_test(
    name="psm_0_1_26",
    url="https://github.com/rust-lang/stacker.git",
    rev="479529695172bb5bd86167fc24cb5644e90b62db",
    manifest="psm",
    lockfile="$(S)/tst/projects/psm_0_1_26/Cargo.lock",
)

pulldown_cmark_escape_0_11_0 = add_project_test(
    name="pulldown_cmark_escape_0_11_0",
    url="https://github.com/raphlinus/pulldown-cmark.git",
    rev="d7632acbe066b81a7f4b23c72fdb18c85f9cb1ea",
    manifest="pulldown-cmark-escape",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/pulldown_cmark_escape_0_11_0/Cargo.lock",
)

punycode_0_4_1 = add_project_test(
    name="punycode_0_4_1",
    url="https://github.com/mcarton/rust-punycode.git",
    rev="89fe5b3a74de1dd2416d62525433b2aa676ae987",
    lockfile="$(S)/tst/projects/punycode_0_4_1/Cargo.lock",
)

rand_chacha_0_9_0 = add_project_test(
    name="rand_chacha_0_9_0",
    url="https://github.com/rust-random/rand.git",
    rev="96f8df65ee6b4368d91a006f9c5b4a8050abae49",
    manifest="rand_chacha",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/rand_chacha_0_9_0/Cargo.lock",
)

rand_core_0_9_3 = add_project_test(
    name="rand_core_0_9_3",
    url="https://github.com/rust-random/rand.git",
    rev="340849e53b71da0f15af448d10511c2e62e50ba1",
    manifest="rand_core",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/rand_core_0_9_3/Cargo.lock",
)

rand_xoshiro_0_7_0 = add_project_test(
    name="rand_xoshiro_0_7_0",
    url="https://github.com/rust-random/rngs.git",
    rev="242e4bdaf8d43a59d84c5511f427026026ea4c97",
    manifest="rand_xoshiro",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/rand_xoshiro_0_7_0/Cargo.lock",
)

regex_automata_0_4_9 = add_project_test(
    name="regex_automata_0_4_9",
    url="https://github.com/rust-lang/regex.git",
    rev="1a069b9232c607b34c4937122361aa075ef573fa",
    manifest="regex-automata",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/regex_automata_0_4_9/Cargo.lock",
)

regex_syntax_0_8_5 = add_project_test(
    name="regex_syntax_0_8_5",
    url="https://github.com/rust-lang/regex.git",
    rev="cba0fbc0194456f644040d7558ae6ed261d57cc2",
    manifest="regex-syntax",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/regex_syntax_0_8_5/Cargo.lock",
)

rustc_apfloat_0_2_3 = add_project_test(
    name="rustc_apfloat_0_2_3",
    url="https://github.com/rust-lang/rustc_apfloat.git",
    rev="eeaacad81247af65d4043cb3e32d023a652d7951",
    lockfile="$(S)/tst/projects/rustc_apfloat_0_2_3/Cargo.lock",
)

rustc_demangle_0_1_25 = add_project_test(
    name="rustc_demangle_0_1_25",
    url="https://github.com/rust-lang/rustc-demangle.git",
    rev="8e15996082029a8f5e577906b7464aa412660d91",
    lockfile="$(S)/tst/projects/rustc_demangle_0_1_25/Cargo.lock",
)

rustc_literal_escaper_0_0_5 = add_project_test(
    name="rustc_literal_escaper_0_0_5",
    url="https://github.com/rust-lang/literal-escaper.git",
    rev="f727a4df1839f0db87bd24d1aadf696ac030fe53",
    lockfile="$(S)/tst/projects/rustc_literal_escaper_0_0_5/Cargo.lock",
)

rustc_stable_hash_0_1_2 = add_project_test(
    name="rustc_stable_hash_0_1_2",
    url="https://github.com/rust-lang/rustc-stable-hash.git",
    rev="bda19e8681a381715fc3f32ee95aa5fda0a7f5d0",
    lockfile="$(S)/tst/projects/rustc_stable_hash_0_1_2/Cargo.lock",
)

rustix_1_0_8 = add_project_test(
    name="rustix_1_0_8",
    url="https://github.com/bytecodealliance/rustix.git",
    rev="5b104ec6c0fd8855b341d6a2c8edf72843ad6cee",
    lockfile="$(S)/tst/projects/rustix_1_0_8/Cargo.lock",
)

ruzstd_0_7_3 = add_project_test(
    name="ruzstd_0_7_3",
    url="https://github.com/KillingSpark/zstd-rs.git",
    rev="6b371baa8b8656bb0a14056b71377df1f1f9e50c",
    lockfile="$(S)/tst/projects/ruzstd_0_7_3/Cargo.lock",
)

scoped_tls_1_0_1 = add_project_test(
    name="scoped_tls_1_0_1",
    url="https://github.com/alexcrichton/scoped-tls.git",
    rev="c0ff7bf6d33e568353ed863d90f893e7e80a0ed1",
    lockfile="$(S)/tst/projects/scoped_tls_1_0_1/Cargo.lock",
)

self_cell_1_2_0 = add_project_test(
    name="self_cell_1_2_0",
    url="https://github.com/Voultapher/self_cell.git",
    rev="5861fcdb2ca37af12387dcee6e1bdb0b5b84db0f",
    lockfile="$(S)/tst/projects/self_cell_1_2_0/Cargo.lock",
)

serde_path_to_error_0_1_17 = add_project_test(
    name="serde_path_to_error_0_1_17",
    url="https://github.com/dtolnay/path-to-error.git",
    rev="9cb82546aaba7371a9b0c486d2aeab2e3ea23a2b",
    lockfile="$(S)/tst/projects/serde_path_to_error_0_1_17/Cargo.lock",
)

sha1_0_10_6 = add_project_test(
    name="sha1_0_10_6",
    url="https://github.com/RustCrypto/hashes.git",
    rev="7aba4b52715f9cb17a90303bab55bd59471d65ae",
    manifest="sha1",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/sha1_0_10_6/Cargo.lock",
)

sharded_slab_0_1_7 = add_project_test(
    name="sharded_slab_0_1_7",
    url="https://github.com/hawkw/sharded-slab.git",
    rev="40579b92debe2ef283a455eb379945e023080ff3",
    lockfile="$(S)/tst/projects/sharded_slab_0_1_7/Cargo.lock",
)

shlex_1_3_0 = add_project_test(
    name="shlex_1_3_0",
    url="https://github.com/comex/rust-shlex.git",
    rev="4a0724b0b62ef715467875b040a890ce75a8a829",
    lockfile="$(S)/tst/projects/shlex_1_3_0/Cargo.lock",
)

stable_deref_trait_1_2_0 = add_project_test(
    name="stable_deref_trait_1_2_0",
    url="https://github.com/storyyeller/stable_deref_trait.git",
    rev="66f9d8a15b7209c45f58edee6c1b6bb497b7bd31",
    lockfile="$(S)/tst/projects/stable_deref_trait_1_2_0/Cargo.lock",
)

stacker_0_1_21 = add_project_test(
    name="stacker_0_1_21",
    url="https://github.com/rust-lang/stacker.git",
    rev="7a3ff32d72bcd0a12a938abb21deddf9f1449cdc",
    lockfile="$(S)/tst/projects/stacker_0_1_21/Cargo.lock",
)

static_assertions_1_1_0 = add_project_test(
    name="static_assertions_1_1_0",
    url="https://github.com/nvzqz/static-assertions-rs.git",
    rev="18bc65a094d890fe1faa5d3ccb70f12b89eabf56",
    lockfile="$(S)/tst/projects/static_assertions_1_1_0/Cargo.lock",
)

synstructure_0_13_2 = add_project_test(
    name="synstructure_0_13_2",
    url="https://github.com/mystor/synstructure.git",
    rev="91ead072fa43c55f35880bd7f75a2b0eab72a04e",
    lockfile="$(S)/tst/projects/synstructure_0_13_2/Cargo.lock",
)

termcolor_1_4_1 = add_project_test(
    name="termcolor_1_4_1",
    url="https://github.com/BurntSushi/termcolor.git",
    rev="71f0921f1eeceda85487098588a1602979d52493",
    lockfile="$(S)/tst/projects/termcolor_1_4_1/Cargo.lock",
)

termize_0_2_0 = add_project_test(
    name="termize_0_2_0",
    url="https://github.com/JohnTitor/termize.git",
    rev="f4cd65bd889c15f10f0a49fd4ee4b8b730830b6c",
    lockfile="$(S)/tst/projects/termize_0_2_0/Cargo.lock",
)

thin_vec_0_2_14 = add_project_test(
    name="thin_vec_0_2_14",
    url="https://github.com/gankra/thin-vec.git",
    rev="ce8808956191d55fe5a8aedcefc7ca3a94ddea11",
    lockfile="$(S)/tst/projects/thin_vec_0_2_14/Cargo.lock",
)

thiserror_impl_2_0_12 = add_project_test(
    name="thiserror_impl_2_0_12",
    url="https://github.com/dtolnay/thiserror.git",
    rev="95a512669395f30cf9ae10343149726c0563ed76",
    manifest="impl",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/thiserror_impl_2_0_12/Cargo.lock",
)

thorin_dwp_0_9_0 = add_project_test(
    name="thorin_dwp_0_9_0",
    url="https://github.com/rust-lang/thorin.git",
    rev="256c3d1e89ba375f64a9b32965678cf905ef7745",
    manifest="thorin",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/thorin_dwp_0_9_0/Cargo.lock",
)

thread_local_1_1_9 = add_project_test(
    name="thread_local_1_1_9",
    url="https://github.com/Amanieu/thread_local-rs.git",
    rev="4b7cc0f30b81b768fafc4b0ef4d541bfc94a0433",
    lockfile="$(S)/tst/projects/thread_local_1_1_9/Cargo.lock",
)

tinyvec_macros_0_1_1 = add_project_test(
    name="tinyvec_macros_0_1_1",
    url="https://github.com/Soveu/tinyvec_macros.git",
    rev="860c23a09d91c8b9203134a81de7888b7191d5f2",
    lockfile="$(S)/tst/projects/tinyvec_macros_0_1_1/Cargo.lock",
)

tracing_core_0_1_30 = add_project_test(
    name="tracing_core_0_1_30",
    url="https://github.com/tokio-rs/tracing.git",
    rev="8b01ea9b9c0dfc06ab101940c94e23934c4d4cc8",
    manifest="tracing-core",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/tracing_core_0_1_30/Cargo.lock",
)

tracing_log_0_2_0 = add_project_test(
    name="tracing_log_0_2_0",
    url="https://github.com/tokio-rs/tracing.git",
    rev="4161d8137d4f6117f17b110b0ec022d9350bf8e6",
    manifest="tracing-log",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/tracing_log_0_2_0/Cargo.lock",
)

tracing_tree_0_3_1 = add_project_test(
    name="tracing_tree_0_3_1",
    url="https://github.com/davidbarsky/tracing-tree.git",
    rev="bbe6596fbe5e0bd637c40c955be55d7a569e8668",
    lockfile="$(S)/tst/projects/tracing_tree_0_3_1/Cargo.lock",
    vendor_sync=["test_dependencies/Cargo.toml"],
)

twox_hash_1_6_3 = add_project_test(
    name="twox_hash_1_6_3",
    url="https://github.com/shepmaster/twox-hash.git",
    rev="79168f770e0e870a11c5bb69ec0382547ef04790",
    lockfile="$(S)/tst/projects/twox_hash_1_6_3/Cargo.lock",
)

type_map_0_5_1 = add_project_test(
    name="type_map_0_5_1",
    url="https://github.com/kardeiz/type-map.git",
    rev="d3ab3e642736d87f3fa8b1e6c8f13a93fcfb72e1",
    lockfile="$(S)/tst/projects/type_map_0_5_1/Cargo.lock",
)

typenum_1_18_0 = add_project_test(
    name="typenum_1_18_0",
    url="https://github.com/paholg/typenum.git",
    rev="67584b536d97c5503fa0f8d6ea1162eb4d9b8383",
    lockfile="$(S)/tst/projects/typenum_1_18_0/Cargo.lock",
)

unic_langid_0_9_6 = add_project_test(
    name="unic_langid_0_9_6",
    url="https://github.com/zbraniecki/unic-locale.git",
    rev="4f37b35b55ab2354319abe11db6e84fe83abe895",
    manifest="unic-langid",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/unic_langid_0_9_6/Cargo.lock",
)

unic_langid_impl_0_9_6 = add_project_test(
    name="unic_langid_impl_0_9_6",
    url="https://github.com/zbraniecki/unic-locale.git",
    rev="4f37b35b55ab2354319abe11db6e84fe83abe895",
    manifest="unic-langid-impl",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/unic_langid_impl_0_9_6/Cargo.lock",
)

unic_langid_macros_0_9_6 = add_project_test(
    name="unic_langid_macros_0_9_6",
    url="https://github.com/zbraniecki/unic-locale.git",
    rev="4f37b35b55ab2354319abe11db6e84fe83abe895",
    manifest="unic-langid-macros",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/unic_langid_macros_0_9_6/Cargo.lock",
)

unic_langid_macros_impl_0_9_6 = add_project_test(
    name="unic_langid_macros_impl_0_9_6",
    url="https://github.com/zbraniecki/unic-locale.git",
    rev="4f37b35b55ab2354319abe11db6e84fe83abe895",
    manifest="unic-langid-macros-impl",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/unic_langid_macros_impl_0_9_6/Cargo.lock",
)

unicase_2_8_1 = add_project_test(
    name="unicase_2_8_1",
    url="https://github.com/seanmonstar/unicase.git",
    rev="c42d9624fe9043607820064c3bd375f809b7b808",
    lockfile="$(S)/tst/projects/unicase_2_8_1/Cargo.lock",
)

unicode_ident_1_0_18 = add_project_test(
    name="unicode_ident_1_0_18",
    url="https://github.com/dtolnay/unicode-ident.git",
    rev="93ab72c02e41056e63a3414d80878d18f2f7a962",
    lockfile="$(S)/tst/projects/unicode_ident_1_0_18/Cargo.lock",
)

unicode_properties_0_1_3 = add_project_test(
    name="unicode_properties_0_1_3",
    url="https://github.com/unicode-rs/unicode-properties.git",
    rev="d36bbc07c2a240a23828bbf1c0f05057035770dc",
    lockfile="$(S)/tst/projects/unicode_properties_0_1_3/Cargo.lock",
)

unicode_script_0_5_7 = add_project_test(
    name="unicode_script_0_5_7",
    url="https://github.com/unicode-rs/unicode-script.git",
    rev="90bd26b53764c3f36b16f545a299792db31a650f",
    lockfile="$(S)/tst/projects/unicode_script_0_5_7/Cargo.lock",
)

unicode_security_0_1_2 = add_project_test(
    name="unicode_security_0_1_2",
    url="https://github.com/unicode-rs/unicode-security.git",
    rev="22d684a34c3e16803555d5f5f006b194772bd155",
    lockfile="$(S)/tst/projects/unicode_security_0_1_2/Cargo.lock",
)

unicode_xid_0_2_6 = add_project_test(
    name="unicode_xid_0_2_6",
    url="https://github.com/unicode-rs/unicode-xid.git",
    rev="5d50587f19713cd35f1f48e5d4d011aa9c54dbff",
    lockfile="$(S)/tst/projects/unicode_xid_0_2_6/Cargo.lock",
)

version_check_0_9_5 = add_project_test(
    name="version_check_0_9_5",
    url="https://github.com/SergioBenitez/version_check.git",
    rev="d77ef9f27cc336719b2d839d09ee6635dd22f758",
    lockfile="$(S)/tst/projects/version_check_0_9_5/Cargo.lock",
)

wasm_encoder_0_219_2 = add_project_test(
    name="wasm_encoder_0_219_2",
    url="https://github.com/bytecodealliance/wasm-tools.git",
    rev="3a7d60de4be52c24e4392548af16c4bd5075bff3",
    manifest="crates/wasm-encoder",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/wasm_encoder_0_219_2/Cargo.lock",
)

wasmparser_0_234_0 = add_project_test(
    name="wasmparser_0_234_0",
    url="https://github.com/bytecodealliance/wasm-tools.git",
    rev="083589ac255bb867e928b6564f5550b3770fb9e8",
    manifest="crates/wasmparser",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/wasmparser_0_234_0/Cargo.lock",
)

writeable_0_5_5 = add_project_test(
    name="writeable_0_5_5",
    url="https://github.com/unicode-org/icu4x.git",
    rev="55cd12ebb25c6261492e1e3dfa2e6453c54dde31",
    manifest="utils/writeable",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/writeable_0_5_5/Cargo.lock",
)

yoke_derive_0_7_5 = add_project_test(
    name="yoke_derive_0_7_5",
    url="https://github.com/unicode-org/icu4x.git",
    rev="6bd4893cc44c2ca2718de47a119a31cc40045fe5",
    manifest="utils/yoke/derive",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/yoke_derive_0_7_5/Cargo.lock",
)

zerovec_derive_0_10_3 = add_project_test(
    name="zerovec_derive_0_10_3",
    url="https://github.com/unicode-org/icu4x.git",
    rev="e1193d0f5d343317857570e9c4e8a03c87875d8b",
    manifest="utils/zerovec/derive",
    vendor_manifest=".",
    lockfile="$(S)/tst/projects/zerovec_derive_0_10_3/Cargo.lock",
)


# Unit regressions: one self-contained tst/unit/test_*.rs per compiler fix,
# each its own node — compiled against the shared libstd and run (must exit 0).
unit_tests = [
    command(
        name="unit_doctest_import",
        inputs=[
            "$(S)/tst/rust_doctest/import.py",
            "$(S)/tst/rust_doctest/test_import.py",
            *TIMEOUT_INPUT,
        ],
        outputs=["$(B)/tst/unit/doctest_import.stamp"],
        cmd=[
            [
                *TEST_TIMEOUT,
                "python3",
                "$(S)/tst/rust_doctest/test_import.py",
                "-v",
            ],
            [
                *TEST_TIMEOUT,
                "sh",
                "-c",
                "> $(B)/tst/unit/doctest_import.stamp",
            ],
        ],
        descr="UT",
        color="green",
    )
]
rustc_ut_run = command(
    name="unit_rustc_ut",
    inputs=[*UT_SRC, *TIMEOUT_INPUT],
    outputs=["$(B)/tst/unit/rustc_ut.stamp"],
    cmd=[
        [*TEST_TIMEOUT, "$(B)/tst/unit/rustc_ut"],
        [*TEST_TIMEOUT, "sh", "-c", "> $(B)/tst/unit/rustc_ut.stamp"],
    ],
    deps=[rustc_ut],
    descr="UT",
    color="green",
)
unit_tests.append(rustc_ut_run)
unit_tests.append(cargo_test)
style = []
if not system_rustc_mode:
    style.append(command(
        name="unit_static_storage",
        inputs=[
            "$(S)/dev/static_storage_gate.py",
            "$(B)/tst/unit/rustc_storage_objects.a",
            *TIMEOUT_INPUT,
        ],
        outputs=["$(B)/tst/unit/static_storage.stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/dev/static_storage_gate.py",
            "$(B)/tst/unit/rustc_storage_objects.a",
            "$(B)/tst/unit/static_storage.stamp",
        ],
        deps=[rustc_storage_objects],
        descr="UT",
        color="green",
    ))
COMMENT_SOURCES = [
    *build.glob("$(S)/bin/**/*.h"),
    *build.glob("$(S)/bin/**/*.cpp"),
]
style += [
    command(
        name="style_line_comments",
        inputs=[
            "$(S)/dev/comment_gate.py",
            *COMMENT_SOURCES,
            *TIMEOUT_INPUT,
        ],
        outputs=["$(B)/tst/style/line_comments.stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/dev/comment_gate.py",
            "--stamp", "$(B)/tst/style/line_comments.stamp",
            *COMMENT_SOURCES,
        ],
        descr="ST",
        color="green",
    ),
    command(
        name="unit_node_cast",
        inputs=[
            "$(S)/tst/unit/test_node_cast.py",
            "$(S)/bin/rustc/common.h",
            *build.glob("$(S)/bin/rustc/**/*.h"),
            *build.glob("$(S)/bin/rustc/**/*.cpp"),
            *build.glob("$(S)/bin/rustc/**/*.inc"),
            *TIMEOUT_INPUT,
        ],
        outputs=["$(B)/tst/unit/node_cast.stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/unit/test_node_cast.py",
            "$(S)/bin/rustc", "$(B)/tst/unit/node_cast_test",
            "$(B)/tst/unit/node_cast.stamp",
        ],
        deps=[node_cast_test],
        descr="UT",
        color="green",
    ),
    command(
        name="unit_std_ratchet",
        # Rewrites dev/std_ratchet.baseline in the source tree when the
        # count shrinks: that must happen here, not in a remote mirror.
        local=True,
        inputs=[
            "$(S)/dev/std_ratchet.py",
            "$(S)/dev/std_ratchet.baseline",
            *build.glob("$(S)/bin/rustc/**/*.h"),
            *build.glob("$(S)/bin/rustc/**/*.cpp"),
            *TIMEOUT_INPUT,
        ],
        outputs=["$(B)/tst/unit/std_ratchet.stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/dev/std_ratchet.py",
            "--baseline", "$(S)/dev/std_ratchet.baseline",
            "--stamp", "$(B)/tst/unit/std_ratchet.stamp",
            *build.glob("$(S)/bin/rustc/**/*.h"),
            *build.glob("$(S)/bin/rustc/**/*.cpp"),
        ],
        descr="UT",
        color="green",
    ),
    command(
        name="unit_std_ratchet_lexer",
        inputs=[
            "$(S)/tst/unit/test_std_ratchet.py",
            "$(S)/dev/std_ratchet.py",
            *TIMEOUT_INPUT,
        ],
        outputs=["$(B)/tst/unit/std_ratchet_lexer.stamp"],
        cmd=[
            [
                *TEST_TIMEOUT,
                "python3", "$(S)/tst/unit/test_std_ratchet.py", "-v",
            ],
            [
                *TEST_TIMEOUT,
                "sh", "-c", "> $(B)/tst/unit/std_ratchet_lexer.stamp",
            ],
        ],
        descr="UT",
        color="green",
    ),
]

for source in sorted(RUSTC_CPP_SRC):
    stem = source.rsplit("/", 1)[1][:-len(".cpp")]
    stamp = f"$(B)/tst/unit/stl_namespace/{stem}.stamp"
    style.append(command(
        name=f"unit_stl_namespace_{stem}",
        inputs=[
            "$(S)/dev/stl_namespace_gate.py",
            source,
            *TIMEOUT_INPUT,
        ],
        outputs=[stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/dev/stl_namespace_gate.py",
            source, stamp,
        ],
        descr="UT",
        color="green",
    ))

unit_tests.append(command(
    name="unit_ident_ordering",
    inputs=["$(S)/tst/unit/test_ident_ordering.cpp", *TIMEOUT_INPUT],
    outputs=["$(B)/tst/unit/ident_ordering.stamp"],
    cmd=[
        [*TEST_TIMEOUT, "$(B)/tst/unit/ident_ordering_test"],
        [*TEST_TIMEOUT, "sh", "-c", "> $(B)/tst/unit/ident_ordering.stamp"],
    ],
    deps=[ident_ordering_test],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_std_source_adjustments",
    inputs=[
        "$(S)/tst/unit/test_std_source_adjustments.py",
        "$(S)/tst/std/fetch.py",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/std_source_adjustments.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3",
            "$(S)/tst/unit/test_std_source_adjustments.py",
            "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh",
            "-c",
            "> $(B)/tst/unit/std_source_adjustments.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
style.append(command(
    name="unit_rustc_header_pairs",
    inputs=[
        "$(S)/tst/unit/test_rustc_header_pairs.py",
        *build.glob("$(S)/bin/rustc/*.h"),
        *build.glob("$(S)/bin/rustc/*.cpp"),
        *build.glob("$(S)/bin/rustc/*.inc"),
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/rustc_header_pairs.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3",
            "$(S)/tst/unit/test_rustc_header_pairs.py",
            "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh",
            "-c",
            "> $(B)/tst/unit/rustc_header_pairs.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
style.append(command(
    name="unit_compiler_no_dead_branches",
    inputs=[
        "$(S)/tst/unit/test_compiler_no_dead_branches.py",
        *build.glob("$(S)/bin/rustc/**/*.h"),
        *build.glob("$(S)/bin/rustc/**/*.cpp"),
        *build.glob("$(S)/bin/rustc/**/*.inc"),
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/compiler_no_dead_branches.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3",
            "$(S)/tst/unit/test_compiler_no_dead_branches.py",
            "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh",
            "-c",
            "> $(B)/tst/unit/compiler_no_dead_branches.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
style.append(command(
    name="unit_compiler_no_const_cast",
    inputs=[
        "$(S)/tst/unit/test_compiler_no_const_cast.py",
        *build.glob("$(S)/bin/rustc/**/*.h"),
        *build.glob("$(S)/bin/rustc/**/*.cpp"),
        *build.glob("$(S)/bin/rustc/**/*.inc"),
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/compiler_no_const_cast.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3",
            "$(S)/tst/unit/test_compiler_no_const_cast.py",
            "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh",
            "-c",
            "> $(B)/tst/unit/compiler_no_const_cast.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_rust_lib_import",
    inputs=[
        "$(S)/tst/rust_lib/import.py",
        "$(S)/tst/rust_lib/case.py",
        "$(S)/tst/rust_lib/test_import.py",
        "$(S)/tst/rust_lib/cases.tsv",
        "$(S)/tst/rust_lib/excluded_cases.tsv",
        "$(S)/tst/rust_lib/groups.tsv",
        "$(S)/tst/rust_lib/upstream/coretests/preamble.rs",
        "$(S)/tst/rust_lib/upstream/coretests/tests/ops.rs",
        "$(S)/tst/rust_lib/upstream/coretests/tests/ops/control_flow.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/rust_lib_import.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3",
            "$(S)/tst/rust_lib/test_import.py",
            "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh",
            "-c",
            "> $(B)/tst/unit/rust_lib_import.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_target_version_default",
    inputs=[
        "$(S)/tst/unit/test_target_version_default.py",
        *build.glob("$(S)/bin/rustc/*.h"),
        *build.glob("$(S)/bin/rustc/*.cpp"),
        *build.glob("$(S)/bin/rustc/*.inc"),
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/target_version_default.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_target_version_default.py",
        "$(B)/tst/unit/target_version_default.stamp",
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_no_windows_support",
    inputs=[
        "$(S)/tst/unit/test_no_windows_support.py",
        "$(S)/tst/unit/test_no_core_main_without_start.rs",
        *build.glob("$(S)/bin/rustc/*.h"),
        *build.glob("$(S)/bin/rustc/*.cpp"),
        *build.glob("$(S)/bin/rustc/*.inc"),
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/no_windows_support.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_no_windows_support.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/test_no_core_main_without_start.rs",
        "$(B)/tst/unit/no_windows_support.stamp",
    ],
    deps=[rustc],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_mir_opt_level",
    inputs=[
        "$(S)/tst/unit/test_mir_opt_level.py",
        "$(S)/tst/unit/mir_opt_level_input.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/mir_opt_level.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_mir_opt_level.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/mir_opt_level_input.rs",
        "$(B)/tst/unit/mir_opt_level.stamp",
    ],
    deps=[rustc],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_codegen_options",
    inputs=[
        "$(S)/tst/unit/test_codegen_options.py",
        "$(S)/tst/unit/mir_opt_level_input.rs",
        "$(S)/tst/unit/codegen_options_cfg.rs",
        "$(S)/tst/unit/codegen_options_link.rs",
        "$(S)/tst/unit/codegen_unwind_cleanup.rs",
        "$(S)/tst/unit/codegen_enum_switch.rs",
        "$(S)/tst/unit/codegen_fieldless_enum_derive.rs",
        "$(S)/tst/unit/codegen_cfg_compaction.rs",
        "$(S)/tst/unit/codegen_prototype_order.rs",
        "$(S)/tst/unit/test_large_function_backend_budget.rs",
        "$(S)/tst/unit/codegen_literal_blob.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/codegen_options.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_codegen_options.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/mir_opt_level_input.rs",
        "$(S)/tst/unit/codegen_options_cfg.rs",
        "$(S)/tst/unit/codegen_options_link.rs",
        "$(S)/tst/unit/codegen_unwind_cleanup.rs",
        "$(S)/tst/unit/codegen_enum_switch.rs",
        "$(S)/tst/unit/codegen_fieldless_enum_derive.rs",
        "$(S)/tst/unit/codegen_cfg_compaction.rs",
        "$(S)/tst/unit/codegen_prototype_order.rs",
        "$(S)/tst/unit/test_large_function_backend_budget.rs",
        "$(S)/tst/unit/codegen_literal_blob.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/codegen_options.stamp",
    ],
    deps=[libstd, rustc],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_local_inner_macros_metadata",
    inputs=[
        "$(S)/tst/unit/test_local_inner_macros_metadata.py",
        "$(S)/tst/unit/local_inner_macros_producer.rs",
        "$(S)/tst/unit/local_inner_macros_consumer.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/local_inner_macros_metadata.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_local_inner_macros_metadata.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/local_inner_macros_producer.rs",
        "$(S)/tst/unit/local_inner_macros_consumer.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/local_inner_macros_metadata.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_qualified_extern_macro_2015",
    inputs=[
        "$(S)/tst/unit/test_qualified_extern_macro_2015.py",
        "$(S)/tst/unit/qualified_extern_macro_2015_producer.rs",
        "$(S)/tst/unit/qualified_extern_macro_2015_consumer.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/qualified_extern_macro_2015.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_qualified_extern_macro_2015.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/qualified_extern_macro_2015_producer.rs",
        "$(S)/tst/unit/qualified_extern_macro_2015_consumer.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/qualified_extern_macro_2015.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_generic_default_projection_metadata",
    inputs=[
        "$(S)/tst/unit/test_generic_default_projection_metadata.py",
        "$(S)/tst/unit/generic_default_projection_metadata_producer.rs",
        "$(S)/tst/unit/generic_default_projection_metadata_consumer.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/generic_default_projection_metadata.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_generic_default_projection_metadata.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/generic_default_projection_metadata_producer.rs",
        "$(S)/tst/unit/generic_default_projection_metadata_consumer.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/generic_default_projection_metadata.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_forward_closure_rpit_metadata",
    inputs=[
        "$(S)/tst/unit/test_forward_closure_rpit_metadata.py",
        "$(S)/tst/unit/forward_closure_rpit_metadata_producer.rs",
        "$(S)/tst/unit/forward_closure_rpit_metadata_consumer.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/forward_closure_rpit_metadata.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_forward_closure_rpit_metadata.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/forward_closure_rpit_metadata_producer.rs",
        "$(S)/tst/unit/forward_closure_rpit_metadata_consumer.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/forward_closure_rpit_metadata.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_const_borrow_offset_metadata",
    inputs=[
        "$(S)/tst/unit/test_const_borrow_offset_metadata.py",
        "$(S)/tst/unit/const_borrow_offset_producer.rs",
        "$(S)/tst/unit/const_borrow_offset_consumer.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/const_borrow_offset_metadata.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_const_borrow_offset_metadata.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/const_borrow_offset_producer.rs",
        "$(S)/tst/unit/const_borrow_offset_consumer.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/const_borrow_offset_metadata.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_inline_markings_metadata_driver",
    inputs=[
        "$(S)/tst/unit/test_inline_markings_metadata.py",
        "$(S)/tst/unit/inline_markings_producer.rs",
        "$(S)/tst/unit/test_inline_markings_metadata.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/inline_markings_metadata_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_inline_markings_metadata.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/inline_markings_producer.rs",
        "$(S)/tst/unit/test_inline_markings_metadata.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/inline_markings_metadata_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_closure_inline_marking",
    inputs=[
        "$(S)/tst/unit/test_closure_inline_marking.py",
        "$(S)/tst/unit/closure_inline_marking_input.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/closure_inline_marking.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_closure_inline_marking.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/closure_inline_marking_input.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/closure_inline_marking.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_attribute_targets",
    inputs=[
        "$(S)/tst/unit/test_attribute_targets.py",
        "$(S)/tst/unit/attribute_targets_input.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/attribute_targets.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_attribute_targets.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/attribute_targets_input.rs",
        "$(B)/tst/unit/attribute_targets.stamp",
    ],
    deps=[rustc],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_trait_object_supertrait_binding",
    inputs=[
        "$(S)/tst/unit/test_trait_object_supertrait_binding.py",
        "$(S)/tst/unit/trait_object_supertrait_binding_input.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/trait_object_supertrait_binding.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_trait_object_supertrait_binding.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/trait_object_supertrait_binding_input.rs",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/trait_object_supertrait_binding.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_driver_lint_cfg_options",
    inputs=[
        "$(S)/tst/unit/test_driver_lint_cfg_options.py",
        "$(S)/tst/unit/driver_lint_cfg_input.rs",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/driver_lint_cfg_options.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_driver_lint_cfg_options.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/driver_lint_cfg_input.rs",
        "$(B)/tst/unit/driver_lint_cfg_options.stamp",
    ],
    deps=[rustc],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_project_xfail",
    inputs=[
        "$(S)/tst/test_project.py",
        "$(S)/tst/unit/test_project_xfail.py",
        *TESTS_LIB,
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/project_xfail.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/unit/test_project_xfail.py", "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh", "-c", "> $(B)/tst/unit/project_xfail.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_libstd_timeout",
    inputs=[
        "$(S)/tst/unit/test_libstd_timeout.py",
        "$(S)/build.py",
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/libstd_timeout.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_libstd_timeout.py",
        "$(S)/build.py", "$(B)/tst/unit/libstd_timeout.stamp",
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_build_output_bytes",
    inputs=[
        "$(S)/build",
        "$(S)/tst/unit/test_build_output_bytes.py",
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/build_output_bytes.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_build_output_bytes.py",
        "$(S)/build", "$(B)/tst/unit/build_output_bytes.stamp",
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_noninteractive_program_runner",
    inputs=[
        "$(S)/tst/program.py",
        "$(S)/tst/unit/test_noninteractive_program_runner.py",
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/noninteractive_program_runner.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_noninteractive_program_runner.py",
        "$(S)/tst/program.py", "$(B)/tst/unit/noninteractive_program_runner.stamp",
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_compiletest_flags",
    inputs=[
        "$(S)/tst/unit/test_compiletest_flags.py",
        "$(S)/tst/lib.py",
        "$(S)/tst/rust_1_90/adapter.py",
        "$(S)/tst/rust_ui_compile/import.py",
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/compiletest_flags.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_compiletest_flags.py",
        "$(B)/tst/unit/compiletest_flags.stamp",
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_attribute",
    inputs=[
        "$(S)/tst/unit/test_proc_macro_attribute.py",
        *build.glob("$(S)/tst/unit/proc_macro_attribute/**/*.toml"),
        *build.glob("$(S)/tst/unit/proc_macro_attribute/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/proc_macro_attribute.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_attribute.py",
        "$(S)/tst/unit/proc_macro_attribute/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_attribute.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_raw_identifiers",
    inputs=[
        "$(S)/tst/unit/test_proc_macro_raw_identifiers.py",
        *build.glob("$(S)/tst/unit/proc_macro_raw_identifiers/**/*.toml"),
        *build.glob("$(S)/tst/unit/proc_macro_raw_identifiers/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/proc_macro_raw_identifiers.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_raw_identifiers.py",
        "$(S)/tst/unit/proc_macro_raw_identifiers/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_raw_identifiers.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_span_location",
    inputs=[
        "$(S)/tst/unit/test_proc_macro_span_location.py",
        *build.glob("$(S)/tst/unit/proc_macro_span_location/**/*.toml"),
        *build.glob("$(S)/tst/unit/proc_macro_span_location/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/proc_macro_span_location.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_span_location.py",
        "$(S)/tst/unit/proc_macro_span_location/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_span_location.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_call_site_locals",
    inputs=[
        "$(S)/tst/unit/test_proc_macro_call_site_locals.py",
        *build.glob("$(S)/tst/unit/proc_macro_call_site_locals/**/*.toml"),
        *build.glob("$(S)/tst/unit/proc_macro_call_site_locals/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/proc_macro_call_site_locals.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_call_site_locals.py",
        "$(S)/tst/unit/proc_macro_call_site_locals/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_call_site_locals.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_token_streams",
    inputs=[
        "$(S)/tst/unit/test_proc_macro_token_streams.py",
        *build.glob("$(S)/tst/unit/proc_macro_token_streams/**/*.toml"),
        *build.glob("$(S)/tst/unit/proc_macro_token_streams/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/proc_macro_token_streams.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_token_streams.py",
        "$(S)/tst/unit/proc_macro_token_streams/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_token_streams.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_named_test",
    inputs=[
        "$(S)/tst/unit/test_proc_macro_named_test.py",
        *build.glob("$(S)/tst/unit/proc_macro_named_test/**/*.toml"),
        *build.glob("$(S)/tst/unit/proc_macro_named_test/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/proc_macro_named_test.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_named_test.py",
        "$(S)/tst/unit/proc_macro_named_test/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_named_test.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_repr_after_derive",
    inputs=[
        "$(S)/tst/unit/test_proc_macro_repr_after_derive.py",
        *build.glob("$(S)/tst/unit/proc_macro_repr_after_derive/**/*.toml"),
        *build.glob("$(S)/tst/unit/proc_macro_repr_after_derive/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/proc_macro_repr_after_derive.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_repr_after_derive.py",
        "$(S)/tst/unit/proc_macro_repr_after_derive/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_repr_after_derive.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_transitive_proc_macro_artifact",
    inputs=[
        "$(S)/tst/unit/test_transitive_proc_macro_artifact.py",
        *build.glob("$(S)/tst/unit/transitive_proc_macro_artifact/**/*.toml"),
        *build.glob("$(S)/tst/unit/transitive_proc_macro_artifact/**/*.rs"),
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/transitive_proc_macro_artifact.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_transitive_proc_macro_artifact.py",
        "$(S)/tst/unit/transitive_proc_macro_artifact/Cargo.toml",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/transitive_proc_macro_artifact.stamp",
    ],
    deps=[libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_system_rustc_mode",
    # Generates the whole build graph in system-rustc mode: it reads the tree.
    local=True,
    inputs=[
        "$(S)/build.py",
        "$(S)/tst/system_rustc.py",
        "$(S)/tst/unit/test_system_rustc_mode.py",
        *TIMEOUT_INPUT,
    ],
    outputs=["$(B)/tst/unit/system_rustc_mode.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/unit/test_system_rustc_mode.py", "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh", "-c", "> $(B)/tst/unit/system_rustc_mode.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_extern_c_zst_argument_driver",
    inputs=[
        "$(S)/tst/unit/test_extern_c_zst_argument.py",
        "$(S)/tst/unit/test_extern_c_zst_argument.rs",
        "$(S)/tst/unit/extern_c_zst_argument.c",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/extern_c_zst_argument_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_extern_c_zst_argument.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/test_extern_c_zst_argument.rs",
        "$(S)/tst/unit/extern_c_zst_argument.c",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/extern_c_zst_argument_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_native_link_search_driver",
    inputs=[
        "$(S)/tst/unit/test_native_link_search.py",
        "$(S)/tst/unit/test_native_link_search.rs",
        "$(S)/tst/unit/native_link_search.c",
        *TESTS_LIB,
    ],
    outputs=["$(B)/tst/unit/native_link_search_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_native_link_search.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/test_native_link_search.rs",
        "$(S)/tst/unit/native_link_search.c",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/native_link_search_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_emit_llvm_ir_driver",
    inputs=["$(S)/tst/unit/test_emit_llvm_ir.py", *TESTS_LIB],
    outputs=["$(B)/tst/unit/emit_llvm_ir_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_emit_llvm_ir.py",
        "$(B)/bin/rustc",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/emit_llvm_ir_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_proc_macro_metadata_driver",
    inputs=["$(S)/tst/unit/test_proc_macro_found_by_its_metadata.py", *TESTS_LIB],
    outputs=["$(B)/tst/unit/proc_macro_metadata_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_proc_macro_found_by_its_metadata.py",
        "$(B)/bin/rustc",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/proc_macro_metadata_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_out_dir_driver",
    inputs=["$(S)/tst/unit/test_out_dir_is_created.py", *TESTS_LIB],
    outputs=["$(B)/tst/unit/out_dir_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_out_dir_is_created.py",
        "$(B)/bin/rustc",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/out_dir_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_error_format_driver",
    inputs=["$(S)/tst/unit/test_error_format.py", *TESTS_LIB],
    outputs=["$(B)/tst/unit/error_format_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_error_format.py",
        "$(B)/bin/rustc",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/error_format_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_print_file_names_driver",
    inputs=["$(S)/tst/unit/test_print_file_names.py", *TESTS_LIB],
    outputs=["$(B)/tst/unit/print_file_names_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_print_file_names.py",
        "$(B)/bin/rustc",
        "$(B)/tst/libstd.tar",
        "$(B)/tst/unit/print_file_names_driver.stamp",
    ],
    deps=[libstd, rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_print_cfg_driver",
    inputs=["$(S)/tst/unit/test_print_cfg.py", *TESTS_LIB],
    outputs=["$(B)/tst/unit/print_cfg_driver.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_print_cfg.py",
        "$(B)/bin/rustc",
        "$(B)/tst/unit/print_cfg_driver.stamp",
    ],
    deps=[rustc],
    env=TOOLCHAIN_ENV,
    descr="UT",
    color="green",
))
rust_unit_tests = []
# Files a unit test pulls in with `#[path]`, which are inputs of every unit
# node because the node cannot tell which test names them.
unit_aux = build.glob("$(S)/tst/unit/aux/**/*.rs") + build.glob("$(S)/tst/unit/support/**/*.rs")

for _src in build.glob("$(S)/tst/unit/test_*.rs"):
    _stem = _src.rsplit("/", 1)[1][len("test_"):-len(".rs")]
    _uses_rust_lib_dependencies = _stem == "rust_lib_dev_dependencies"
    _target = command(
        name="unit_" + _stem,
        inputs=[_src, "$(S)/tst/unit/run_one.py"] + unit_aux + TESTS_LIB,
        outputs=["$(B)/tst/unit/" + _stem + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/unit/run_one.py",
            _src, "$(B)/tst/libstd.tar",
            "$(B)/tst/unit/" + _stem + ".stamp",
        ],
        deps=[libstd, rustc] + ([rust_lib_dependencies] if _uses_rust_lib_dependencies else []),
        env={
            "RUSTC": "$(B)/bin/rustc",
            **SYSTEM_TEST_ENV,
            **({"RUST_LIB_DEPENDENCIES": "$(B)/tst/rust-lib-dependencies.tar"}
               if _uses_rust_lib_dependencies else {}),
        },
        descr="UT",
        color="green",
    )
    unit_tests.append(_target)
    rust_unit_tests.append(_target)

# Compile-time performance regressions are deliberately separate from the
# normal test groups: they are valid programs, but expensive enough to run only
# when performance is being measured.
perf_tests = []
for _src in build.glob("$(S)/tst/perf/test_*.rs"):
    _stem = _src.rsplit("/", 1)[1][len("test_"):-len(".rs")]
    perf_tests.append(command(
        name="perf_" + _stem,
        inputs=[_src, "$(S)/tst/unit/run_one.py"] + TESTS_LIB,
        outputs=["$(B)/tst/perf/" + _stem + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/unit/run_one.py",
            _src, "$(B)/tst/libstd.tar",
            "$(B)/tst/perf/" + _stem + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc", **SYSTEM_TEST_ENV},
        descr="PF",
        color="cyan",
    ))

# Vendored Rust 1.90 run-pass tests. Keep one source file per graph node for
# now; sharding can be added later without changing the checked-in corpus.
rust_1_90_root = Path(__file__).parent / "tst" / "rust_1_90"
rust_1_90_cases = (rust_1_90_root / "cases.txt").read_text().splitlines()
rust_1_90_tests = []
for _case in rust_1_90_cases:
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _src = "$(S)/tst/rust_1_90/upstream/" + _case
    _sidecars = []
    _source_base = rust_1_90_root / "upstream" / _case[:-len(".rs")]
    for _suffix in (".run.stdout", ".run.stderr"):
        if Path(str(_source_base) + _suffix).exists():
            _sidecars.append(
                "$(S)/tst/rust_1_90/upstream/" + _case[:-len(".rs")] + _suffix
            )
    rust_1_90_tests.append(command(
        name="rust_1_90_" + _digest,
        inputs=[
            _src,
            *_sidecars,
            "$(S)/tst/rust_1_90/adapter.py",
            "$(S)/tst/rust_1_90/cases.txt",
            *TESTS_LIB,
        ],
        outputs=["$(B)/tst/rust_1_90/" + _case + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rust_1_90/adapter.py",
            _case, _src, "$(B)/tst/libstd.tar",
            "$(B)/tst/rust_1_90/native/librust_test_helpers.a",
            "$(B)/tst/rust_1_90/" + _case + ".stamp",
        ],
        deps=[libstd, rust_test_helpers, rustc],
        env={"RUSTC": "$(B)/bin/rustc", **SYSTEM_TEST_ENV},
        descr="RP",
        color="green",
    ))

# Positive check-pass/build-pass cases from Rust 1.90.  check-pass is compiled
# as a library through the available full pipeline; failures stay observable.
rust_ui_compile_root = Path(__file__).parent / "tst" / "rust_ui_compile"
rust_ui_compile_cases = json.loads(
    (rust_ui_compile_root / "cases.json").read_text()
)
rust_ui_compile_tests = []
for _index, _case in enumerate(rust_ui_compile_cases):
    _path = _case["path"]
    _digest = hashlib.sha256(_path.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/rust_ui_compile/" + _digest + ".stamp"
    rust_ui_compile_tests.append(command(
        name="rust_ui_compile_" + _digest,
        inputs=[
            "$(S)/tst/rust_ui_compile/adapter.py",
            "$(S)/tst/rust_ui_compile/cases.json",
            "$(S)/tst/rust_ui_compile/upstream/" + _path,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rust_ui_compile/adapter.py",
            "$(S)/tst/rust_ui_compile/cases.json", str(_index), "1",
            "$(S)/tst/rust_ui_compile/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="UC",
        color="green",
    ))

# Preserve the upstream file boundary and let the adapter interpret its dg-*
# invariants.  The imported programs are normalized to ordinary Rust 1.90 and
# use the same standard library as the rest of the semantic corpus.
gccrs_root = Path(__file__).parent / "tst" / "gccrs"
gccrs_cases = (gccrs_root / "cases.txt").read_text().splitlines()
gccrs_case_set = set(gccrs_cases)
gccrs_support = []
for _path in sorted((gccrs_root / "upstream").rglob("*")):
    _relative = _path.relative_to(gccrs_root / "upstream").as_posix()
    if _path.is_file() and _relative not in gccrs_case_set:
        gccrs_support.append("$(S)/tst/gccrs/upstream/" + _relative)

gccrs_tests = []
for _case in gccrs_cases:
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _src = "$(S)/tst/gccrs/upstream/" + _case
    gccrs_tests.append(command(
        name="gccrs_" + _digest,
        inputs=[
            _src,
            *gccrs_support,
            "$(S)/tst/gccrs/adapter.py",
            "$(S)/tst/gccrs/cases.txt",
            "$(B)/tst/libstd.tar",
            *TESTS_LIB,
        ],
        outputs=["$(B)/tst/gccrs/" + _case + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/gccrs/adapter.py",
            _case, _src, "$(B)/tst/libstd.tar",
            "$(B)/tst/gccrs/" + _case + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="GX",
        color="green",
    ))

# Positive gccrs compile-suite inputs have no runtime contract.  Keep every
# crate root in its own node; all copied sources are inputs because a few use mod!.
gccrs_compile_root = Path(__file__).parent / "tst" / "gccrs_compile"
gccrs_compile_cases = (gccrs_compile_root / "cases.txt").read_text().splitlines()
gccrs_compile_sources = [
    "$(S)/tst/gccrs_compile/upstream/"
    + _path.relative_to(gccrs_compile_root / "upstream").as_posix()
    for _path in sorted((gccrs_compile_root / "upstream").rglob("*"))
    if _path.is_file()
]
gccrs_compile_tests = []
for _index, _case in enumerate(gccrs_compile_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/gccrs_compile/" + _digest + ".stamp"
    gccrs_compile_tests.append(command(
        name="gccrs_compile_" + _digest,
        inputs=[
            "$(S)/tst/gccrs_compile/adapter.py",
            "$(S)/tst/gccrs_compile/cases.txt",
            "$(B)/tst/libstd.tar",
            *gccrs_compile_sources,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/gccrs_compile/adapter.py",
            "$(S)/tst/gccrs_compile/cases.txt", str(_index), "1",
            "$(S)/tst/gccrs_compile/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="GC",
        color="green",
    ))

# Rust Quiz programs each print one documented answer.  Store the answer as a
# tiny sidecar instead of vendoring the prose explanations or upstream crate.
rust_quiz_root = Path(__file__).parent / "tst" / "rust_quiz"
rust_quiz_cases = (rust_quiz_root / "cases.txt").read_text().splitlines()
rust_quiz_tests = []
for _case in rust_quiz_cases:
    _number = _case.split("-", 1)[0]
    _src = "$(S)/tst/rust_quiz/upstream/" + _case
    _expected = _src[:-len(".rs")] + ".stdout"
    rust_quiz_tests.append(command(
        name="rust_quiz_" + _number,
        inputs=[
            _src,
            _expected,
            "$(S)/tst/rust_quiz/adapter.py",
            "$(S)/tst/rust_quiz/cases.txt",
            *TESTS_LIB,
        ],
        outputs=["$(B)/tst/rust_quiz/" + _case + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rust_quiz/adapter.py",
            _case, _src, _expected, "$(B)/tst/libstd.tar",
            "$(B)/tst/rust_quiz/" + _case + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="RQ",
        color="green",
    ))

# Official solved Rustlings exercises retain the upstream distinction between
# normal binaries and rustc test harnesses.  Each file is one build node.
rustlings_root = Path(__file__).parent / "tst" / "rustlings"
rustlings_cases = [
    _line.split("\t")
    for _line in (rustlings_root / "cases.tsv").read_text().splitlines()
]
rustlings_tests = []
for _index, (_case, _mode) in enumerate(rustlings_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/rustlings/" + _digest + ".stamp"
    rustlings_tests.append(command(
        name="rustlings_" + _digest,
        inputs=[
            "$(S)/tst/rustlings/adapter.py",
            "$(S)/tst/rustlings/cases.tsv",
            "$(S)/tst/rustlings/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rustlings/adapter.py",
            "$(S)/tst/rustlings/cases.tsv", str(_index), "1",
            "$(S)/tst/rustlings/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="RL",
        color="green",
    ))

# Rust By Example Markdown fences that the reference Rust 1.90 compiler can
# build and run as independent programs.  Preserve each fence as one node.
rust_by_example_root = Path(__file__).parent / "tst" / "rust_by_example"
rust_by_example_cases = [
    _line.split("\t")
    for _line in (rust_by_example_root / "cases.tsv").read_text().splitlines()
]
rust_by_example_tests = []
for _index, (_case, _origin, _edition) in enumerate(rust_by_example_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/rust_by_example/" + _digest + ".stamp"
    rust_by_example_tests.append(command(
        name="rust_by_example_" + _digest,
        inputs=[
            "$(S)/tst/rust_by_example/adapter.py",
            "$(S)/tst/rust_by_example/cases.tsv",
            "$(S)/tst/rust_by_example/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rust_by_example/adapter.py",
            "$(S)/tst/rust_by_example/cases.tsv", str(_index), "1",
            "$(S)/tst/rust_by_example/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="BE",
        color="green",
    ))

# Source-only targets from the official Rust Book listings, reference-checked
# with Rust 1.90.  Copying each tiny source tree preserves module resolution.
rust_book_root = Path(__file__).parent / "tst" / "rust_book"
rust_book_cases = [
    _line.split("\t")
    for _line in (rust_book_root / "cases.tsv").read_text().splitlines()
]
rust_book_tests = []
for _index, (_case, _root, _mode, _edition) in enumerate(rust_book_cases):
    _case_id = "\t".join((_case, _root, _mode, _edition))
    _digest = hashlib.sha256(_case_id.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/rust_book/" + _digest + ".stamp"
    _sources = []
    for _path in sorted((rust_book_root / "upstream" / _case).rglob("*.rs")):
        _relative = _path.relative_to(rust_book_root / "upstream").as_posix()
        _sources.append("$(S)/tst/rust_book/upstream/" + _relative)
    rust_book_tests.append(command(
        name="rust_book_" + _digest,
        inputs=[
            "$(S)/tst/rust_book/adapter.py",
            "$(S)/tst/rust_book/cases.tsv",
            "$(S)/tst/program.py",
            *_sources,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rust_book/adapter.py",
            "$(S)/tst/rust_book/cases.tsv", str(_index), "1",
            "$(S)/tst/rust_book/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="BK",
        color="green",
    ))

# Official Exercism solutions linked to their dependency-free integration
# tests.  The adapter runs ignored tests too; they are only progression gates.
exercism_rust_root = Path(__file__).parent / "tst" / "exercism_rust"
exercism_rust_cases = [
    _line.split("\t")
    for _line in (exercism_rust_root / "cases.tsv").read_text().splitlines()
]
exercism_rust_tests = []
for _index, (_slug, _crate, _edition, _count) in enumerate(exercism_rust_cases):
    _digest = hashlib.sha256(_slug.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/exercism_rust/" + _digest + ".stamp"
    _sources = []
    for _path in sorted((exercism_rust_root / "upstream" / _slug).rglob("*.rs")):
        _relative = _path.relative_to(exercism_rust_root / "upstream").as_posix()
        _sources.append("$(S)/tst/exercism_rust/upstream/" + _relative)
    exercism_rust_tests.append(command(
        name="exercism_rust_" + _digest,
        inputs=[
            "$(S)/tst/exercism_rust/adapter.py",
            "$(S)/tst/exercism_rust/cases.tsv",
            *_sources,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *EXERCISM_TIMEOUT,
            "python3", "$(S)/tst/exercism_rust/adapter.py",
            "$(S)/tst/exercism_rust/cases.tsv", str(_index), "1",
            "$(S)/tst/exercism_rust/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="EX",
        color="green",
    ))

# Code fences from The Rust Reference, including its documented pass, panic,
# no-run, and compile-fail modes.  The importer reference-checks every fence.
rust_reference_root = Path(__file__).parent / "tst" / "rust_reference"
rust_reference_cases = [
    _line.split("\t")
    for _line in (rust_reference_root / "cases.tsv").read_text().splitlines()
]
rust_reference_tests = []
for _index, (_case, _origin, _edition, _mode) in enumerate(rust_reference_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/rust_reference/" + _digest + ".stamp"
    rust_reference_tests.append(command(
        name="rust_reference_" + _digest,
        inputs=[
            "$(S)/tst/rust_reference/adapter.py",
            "$(S)/tst/rust_reference/cases.tsv",
            "$(S)/tst/rust_reference/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rust_reference/adapter.py",
            "$(S)/tst/rust_reference/cases.tsv", str(_index), "1",
            "$(S)/tst/rust_reference/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="RF",
        color="green",
    ))

# Self-contained Rustonomicon code fences, reference-checked with the same
# pass/compile/fail semantics as The Rust Reference.  Each fence is one node.
nomicon_root = Path(__file__).parent / "tst" / "nomicon"
nomicon_cases = [
    _line.split("\t")
    for _line in (nomicon_root / "cases.tsv").read_text().splitlines()
]
nomicon_tests = []
for _index, (_case, _origin, _edition, _mode) in enumerate(nomicon_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/nomicon/" + _digest + ".stamp"
    nomicon_tests.append(command(
        name="nomicon_" + _digest,
        inputs=[
            "$(S)/tst/nomicon/adapter.py",
            "$(S)/tst/nomicon/cases.tsv",
            "$(S)/tst/nomicon/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/nomicon/adapter.py",
            "$(S)/tst/nomicon/cases.tsv", str(_index), "1",
            "$(S)/tst/nomicon/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="NM",
        color="green",
    ))

# The std-only, standalone subset of the official Async Book.  Most of that
# book requires external executor crates; these four fences do not.
async_book_root = Path(__file__).parent / "tst" / "async_book"
async_book_cases = [
    _line.split("\t")
    for _line in (async_book_root / "cases.tsv").read_text().splitlines()
]
async_book_tests = []
for _index, (_case, _origin, _edition, _mode) in enumerate(async_book_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/async_book/" + _digest + ".stamp"
    async_book_tests.append(command(
        name="async_book_" + _digest,
        inputs=[
            "$(S)/tst/async_book/adapter.py",
            "$(S)/tst/async_book/cases.tsv",
            "$(S)/tst/async_book/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/async_book/adapter.py",
            "$(S)/tst/async_book/cases.tsv", str(_index), "1",
            "$(S)/tst/async_book/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="AB",
        color="green",
    ))

# Every explicit Rust library #[test] is an independent compile-and-run node.
# The case adapter disables all other tests in a temporary source overlay.
rust_lib_root = Path(__file__).parent / "tst" / "rust_lib"
rust_lib_group_specs = {}
for _line in (rust_lib_root / "groups.tsv").read_text().splitlines():
    _suite, _harness_group, _kind, _root, _edition = _line.split("\t")
    _key = (_suite, _harness_group)
    if _key in rust_lib_group_specs:
        raise RuntimeError(f"duplicate rust_lib group: {_key}")
    rust_lib_group_specs[_key] = (_kind, _root, _edition)
rust_lib_cases = []
for _line in (rust_lib_root / "cases.tsv").read_text().splitlines():
    rust_lib_cases.append(_line.split("\t"))
rust_lib_sources_by_suite = {}
for _path in sorted((rust_lib_root / "upstream").rglob("*")):
    if not _path.is_file():
        continue
    _relative = _path.relative_to(rust_lib_root / "upstream")
    rust_lib_sources_by_suite.setdefault(_relative.parts[0], []).append(
        "$(S)/tst/rust_lib/upstream/" + _relative.as_posix()
    )
rust_lib_adapters = [
    "$(S)/tst/rust_lib/adapter/"
    + _path.relative_to(rust_lib_root / "adapter").as_posix()
    for _path in sorted((rust_lib_root / "adapter").rglob("*"))
    if _path.is_file()
]

rust_lib_tests = []
slow_rust_lib_tests = []
rust_lib_tests_by_group = {}
slow_rust_lib_cases = {
    "coretests/num/flt2dec/random.rs::num::flt2dec::random::shortest_f32_exhaustive_equivalence_test",
    "coretests/num/flt2dec/random.rs::num::flt2dec::random::shortest_f64_hard_random_equivalence_test",
    "coretests/slice.rs::slice::select_nth_unstable",
}
found_slow_rust_lib_cases = set()
for _suite, _harness_group, _source, _function, _hint in rust_lib_cases:
    _case = _suite + "/" + _source + "::" + _hint
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _key = (_suite, _harness_group)
    if _key not in rust_lib_group_specs:
        raise RuntimeError(f"unknown rust_lib group for {_case}: {_key}")
    _kind, _root, _edition = rust_lib_group_specs[_key]
    _stamp = "$(B)/tst/rust_lib/cases/" + _digest + ".stamp"
    _target = command(
        name="rust_lib_" + _digest,
        inputs=[
            *rust_lib_sources_by_suite[_suite],
            *rust_lib_adapters,
            "$(S)/tst/rust_lib/case.py",
            "$(S)/tst/rust_lib/import.py",
            "$(S)/tst/rust_lib/cases.tsv",
            "$(S)/tst/rust_lib/groups.tsv",
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *LIBSTD_TIMEOUT,
            "python3", "$(S)/tst/rust_lib/case.py",
            _suite, _harness_group, _kind, _root, _edition,
            _source, _function, _hint,
            "$(S)/tst/rust_lib/upstream", "$(B)/tst/libstd.tar",
            "$(B)/tst/rust-lib-dependencies.tar", _stamp,
        ],
        deps=[libstd, rust_lib_dependencies, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="LT",
        color="green",
    )
    if _case in slow_rust_lib_cases:
        slow_rust_lib_tests.append(_target)
        found_slow_rust_lib_cases.add(_case)
    else:
        rust_lib_tests.append(_target)
        rust_lib_tests_by_group.setdefault(_key, []).append(_target)
if found_slow_rust_lib_cases != slow_rust_lib_cases:
    raise RuntimeError(
        "missing slow rust_lib cases: "
        + ", ".join(sorted(slow_rust_lib_cases - found_slow_rust_lib_cases))
    )

# Runnable library documentation examples, extracted into standalone
# programs so each fence remains an independent compile-and-run node.
rust_doctest_root = Path(__file__).parent / "tst" / "rust_doctest"
rust_doctest_cases = []
for _line in (rust_doctest_root / "cases.tsv").read_text().splitlines():
    rust_doctest_cases.append(_line.split("\t"))
rust_doctests = []
for _case, _origin, _edition, _mode in rust_doctest_cases:
    _digest = hashlib.sha256((_case + "\t" + _origin).encode()).hexdigest()[:12]
    _src = "$(S)/tst/rust_doctest/upstream/" + _case
    _stamp = "$(B)/tst/rust_doctest/" + _digest + ".stamp"
    rust_doctests.append(command(
        name="rust_doctest_" + _digest,
        inputs=[
            _src,
            "$(S)/tst/rust_doctest/adapter.py",
            "$(S)/tst/rust_doctest/cases.tsv",
            "$(S)/tst/program.py",
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/rust_doctest/adapter.py",
            _origin, _src, _edition, _mode, "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="DT",
        color="green",
    ))

# Fixed RustSmith programs and rustc 1.90 stdout oracles.  Keep each source and
# its oracle in a separate node so failures cannot hide later cases.
rustsmith_root = Path(__file__).parent / "tst" / "rustsmith"
rustsmith_cases = [
    _line.split("\t")
    for _line in (rustsmith_root / "cases.tsv").read_text().splitlines()
]
rustsmith_tests = []
for _index, (_stem, _seed) in enumerate(rustsmith_cases):
    _stamp = "$(B)/tst/rustsmith/" + _stem + ".stamp"
    _inputs = [
        "$(S)/tst/rustsmith/upstream/" + _stem + _suffix
        for _suffix in (".rs", ".args", ".stdout")
    ]
    rustsmith_tests.append(command(
        name="rustsmith_" + _stem,
        inputs=[
            "$(S)/tst/rustsmith/adapter.py",
            "$(S)/tst/rustsmith/cases.tsv",
            *_inputs,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *LIBSTD_TIMEOUT,
            "python3", "$(S)/tst/rustsmith/adapter.py",
            "$(S)/tst/rustsmith/cases.tsv", str(_index), "1",
            "$(S)/tst/rustsmith/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="RS",
        color="green",
    ))

# Self-contained native Miri pass programs from Rust 1.90.  Every upstream
# source is a separate graph node.
miri_root = Path(__file__).parent / "tst" / "miri"
miri_cases = (miri_root / "cases.tsv").read_text().splitlines()
miri_tests = []
for _index, _case in enumerate(miri_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tst/miri/" + _digest + ".stamp"
    miri_tests.append(command(
        name="miri_" + _digest,
        inputs=[
            "$(S)/tst/miri/adapter.py",
            "$(S)/tst/miri/cases.tsv",
            "$(S)/tst/miri/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/miri/adapter.py",
            "$(S)/tst/miri/cases.tsv", str(_index), "1",
            "$(S)/tst/miri/upstream", "$(B)/tst/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
        descr="MI",
        color="green",
    ))

lite_tests = [
    *(rust_unit_tests if system_rustc_mode else unit_tests),
    *style,
    *rust_1_90_tests,
    *rust_ui_compile_tests,
    *gccrs_tests,
    *gccrs_compile_tests,
    *rust_quiz_tests,
    *rustlings_tests,
    *rust_by_example_tests,
    *rust_book_tests,
    *exercism_rust_tests,
    *rust_reference_tests,
    *nomicon_tests,
    *async_book_tests,
    *rust_lib_tests,
    *rust_doctests,
    *rustsmith_tests,
    *miri_tests,
]


def partition_lite_tests(targets):
    if test_partition is None:
        return targets
    group_index, group_count = test_partition
    selected = []
    test_ids = set()
    for target in targets:
        test_id = target.name or target.output or "\0".join(target.outputs)
        if not test_id:
            raise RuntimeError("lite test target has no deterministic identifier")
        if test_id in test_ids:
            raise RuntimeError(f"lite test target added twice: {test_id}")
        test_ids.add(test_id)
        digest = hashlib.sha256(test_id.encode()).digest()
        if int.from_bytes(digest[:8], "big") % group_count == group_index:
            selected.append(target)
    return selected


group("test", *lite_tests)
group("lite_tests", *partition_lite_tests(lite_tests))
group("projects", *project_tests)
group("slow_tests", *project_tests, *slow_rust_lib_tests)
group("unit", *(rust_unit_tests if system_rustc_mode else unit_tests), *style)
group("ut", rustc_ut_run, *style)
group("style", *style)
group("perf", *perf_tests)
group("rust_1_90", *rust_1_90_tests)
group("rust_ui_compile", *rust_ui_compile_tests)
group("gccrs", *gccrs_tests)
group("gccrs_compile", *gccrs_compile_tests)
group("rust_quiz", *rust_quiz_tests)
group("rustlings", *rustlings_tests)
group("rust_by_example", *rust_by_example_tests)
group("rust_book", *rust_book_tests)
group("exercism_rust", *exercism_rust_tests)
group("rust_reference", *rust_reference_tests)
group("nomicon", *nomicon_tests)
group("async_book", *async_book_tests)
group("rust_lib", *rust_lib_tests)
for (_suite, _harness_group), _tests in rust_lib_tests_by_group.items():
    _name = "rust_lib_" + _suite + "_" + _harness_group
    _name = "".join(_char if _char.isalnum() else "_" for _char in _name)
    group(_name, *_tests)
group("rust_doctest", *rust_doctests)
group("rustsmith", *rustsmith_tests)
group("miri", *miri_tests)
