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

# version.cpp expects these from the build system (Makefile filled them from
# git); pin static values so the build stays hermetic.
build.cppflags += [
    "-DVERSION_GIT_ISDIRTY=0",
    '-DVERSION_GIT_FULLHASH="unknown"',
    '-DVERSION_GIT_SHORTHASH="trustme"',
    '-DVERSION_BUILDTIME="unknown"',
    '-DVERSION_GIT_BRANCH="master"',
]

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

SRC = build.glob("$(S)/bin/rustc/*.cpp")
# Compiler C++ unit tests live next to the code they test (x.cpp -> x_ut.cpp)
# and are linked into the rustc_ut runner, not into the compiler.
UT_SRC = sorted(s for s in SRC if s.endswith("_ut.cpp"))
SRC = [s for s in SRC if not s.endswith("_ut.cpp")]

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
        tu_generated_srcs.append(
            {"src": tu_gen_cpp, "inputs": [f"$(S)/bin/rustc/{tu_context}"]}
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
        ldflags=["-lz", "-flto=thin"],
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
            for s in SRC if not s.endswith("/main_bindings.cpp")
        ],
    ],
    output="$(B)/tst/unit/rustc_ut",
    deps=[platform_libstd, float128_ut_vectors, codegen_c_prelude, unicode_nfc_tables],
    ldflags=["-lz"],
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
cargo = command(
    name="cargo",
    inputs=(
        build.glob("$(S)/bin/cargo/**/*.go")
        + ["$(S)/bin/cargo/go.mod", "$(S)/bin/cargo/go.sum", "$(S)/bin/cargo/vendor/modules.txt"]
    ),
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
# Bound the whole test node, including compilation performed by adapters. The
# in-tree wrapper gives ix and Ubuntu identical process-group semantics.
TEST_TIMEOUT = ["python3", TIMEOUT_SCRIPT, "60s"]
# A real project contains a full Cargo graph and starts from archive inputs in a
# fresh directory, so unlike a unit node it cannot reuse a materialised CAS.
PROJECT_TIMEOUT = ["python3", TIMEOUT_SCRIPT, "5m"]
# trybuild compiles its own dependency graph and then a second generated Cargo
# workspace containing the UI cases. Keep the ordinary project budget tight,
# but allow this deliberately nested corpus node to finish from cold archives.
NESTED_PROJECT_TIMEOUT = ["python3", TIMEOUT_SCRIPT, "15m"]
# A from-scratch standard-library build is intentionally much heavier than a
# single test, but it must not leave the graph occupied indefinitely.
LIBSTD_TIMEOUT = ["python3", TIMEOUT_SCRIPT, "10m"]

# std_src: fetch + adjust the rust-1.90 source, add the shim, pack it.
std_src = command(
    name="std_src",
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
        inputs=source_inputs,
        outputs=[source_archive],
        cmd=source_cmd,
        descr="RS",
        color="magenta",
    )

    vendor = command(
        name=name + "_vendor",
        inputs=["$(S)/tst/vendor.py"] + TESTS_LIB,
        outputs=[vendor_archive],
        cmd=[
            "python3", "$(S)/tst/vendor.py",
            source_archive, vendor_manifest, vendor_archive,
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
)

base64 = add_project_test(
    name="base64",
    url="https://github.com/marshallpierce/rust-base64.git",
    rev="069bf7067b949f5c0a92b6ceb82492920502f2c2",
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
)

console_0_7_7 = add_project_test(
    name="console_0_7_7",
    url="https://github.com/mitsuhiko/console.git",
    rev="9f62b487585476f7a5ba85cd6a2b109d6d15a92f",
    lockfile="$(S)/tst/projects/console_0_7_7/Cargo.lock",
)

combine = add_project_test(
    name="combine",
    url="https://github.com/Marwes/combine.git",
    rev="50a71afa1c88e8564e0220a6e0625dd16a2302a2",
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
    adapter_args=["--features", "derive,simd"],
    lockfile="$(S)/tst/projects/zerocopy/Cargo.lock",
)

zerocopy_0_8_56 = add_project_test(
    name="zerocopy_0_8_56",
    url="https://github.com/google/zerocopy.git",
    rev="6dc429c451bdf1d7202ec1ec2cf426514e00d8eb",
    manifest="zerocopy",
    adapter_args=["--features", "derive,simd"],
    lockfile="$(S)/tst/projects/zerocopy_0_8_56/Cargo.lock",
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
unit_tests.append(command(
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
))
unit_tests.append(command(
    name="unit_std_ratchet",
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
unit_tests.append(command(
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
unit_tests.append(command(
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
        *TIMEOUT_INPUT,
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
        "$(B)/bin/rustc",
        "$(B)/tst/unit/target_version_default.stamp",
    ],
    deps=[rustc],
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
rust_unit_tests = []
# Files a unit test pulls in with `#[path]`, which are inputs of every unit
# node because the node cannot tell which test names them.
unit_aux = build.glob("$(S)/tst/unit/aux/**/*.rs")

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
            *TEST_TIMEOUT,
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
group("unit", *(rust_unit_tests if system_rustc_mode else unit_tests))
# `ut` builds+runs only the C++ *_ut.cpp runner (bin/rustc/*_ut.cpp), without the
# much larger `unit` group (which also runs the semantic .rs regression corpus).
group("ut", rustc_ut_run)
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
