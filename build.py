import hashlib
import json
from pathlib import Path

import build

# Build description for rustc (the mrustc-derived Rust compiler).
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

build.includes += ["$(S)/bin/rustc"]

# version.cpp expects these from the build system (Makefile filled them from
# git); pin static values so the build stays hermetic.
build.cppflags += [
    "-DVERSION_GIT_ISDIRTY=0",
    '-DVERSION_GIT_FULLHASH="unknown"',
    '-DVERSION_GIT_SHORTHASH="mrustc"',
    '-DVERSION_BUILDTIME="unknown"',
    '-DVERSION_GIT_BRANCH="master"',
]

build.cxxflags += [
    "-std=c++26",
    "-O2",
    "-g",
]

# Keep the platform library in the same imported build graph as its consumers.
# This mirrors shitty: libstd owns its source discovery and compile flags, while
# the parent graph supplies reproducible paths and links the resulting archive.
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

SRC = build.glob("$(S)/bin/rustc/*.cpp")

rustc = program(
    srcs=SRC,
    name="rustc",
    output="$(B)/bin/rustc",
    deps=[platform_libstd],
    ldflags=["-lz"],
)

# cargo: Cargo-compatible package resolver and mrustc build driver, written in
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
# These are heavy (a from-scratch libstd plus a full project build) and only
# run on request: `./build test`, or a single artifact like `./build resvg`.

TOOLCHAIN_ENV = {
    "RUSTC": "$(B)/bin/rustc",
    "CARGO": "$(B)/bin/cargo",
}

# All node scripts are Python and share tst/lib.py.
TESTS_LIB = ["$(S)/tst/lib.py"]
# Bound the whole test node, including compilation performed by adapters.
# The build engine resolves Nix's `timeout` symlink to the multicall binary,
# so select its applet explicitly instead of relying on argv[0].
TEST_TIMEOUT = ["coreutils", "--coreutils-prog=timeout", "60s"]

# std_src: fetch + adjust the rust-1.90 source, add the shim, pack it.
std_src = command(
    name="std_src",
    inputs=["$(S)/tst/std/fetch.py"] + TESTS_LIB,
    outputs=["$(B)/tst/rust-src.tar"],
    cmd=[
        "python3", "$(S)/tst/std/fetch.py",
        "$(B)/tst/rust-src.tar",
    ],
    descr="RS",
    color="cyan",
)

# libstd: build the standard library (+ libproc_macro) once, from that source.
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
        "python3", "$(S)/tst/std/build.py",
        "$(B)/tst/rust-src.tar", "$(B)/tst/libstd.tar",
        "$(S)/lib/proc_macro/Cargo.toml",
    ],
    deps=[std_src, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="LS",
    color="cyan",
)

rust_lib_dependencies = command(
    name="rust_lib_dependencies",
    inputs=(
        [
            "$(S)/tst/rust_lib/build_dependencies.py",
            "$(S)/tst/rust_lib/dependencies/Cargo.toml",
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

# resvg_src: the project source at a pinned revision.
resvg_src = command(
    name="resvg_src",
    inputs=["$(S)/tst/git_src.py"] + TESTS_LIB,
    outputs=["$(B)/tst/resvg-src.tar"],
    cmd=[
        "python3", "$(S)/tst/git_src.py",
        "https://github.com/linebender/resvg.git",
        "08c79a3148df4ce8ab08fca72204b142b95423dd",
        "$(B)/tst/resvg-src.tar",
    ],
    descr="RS",
    color="magenta",
)

# resvg_vendor: vendor resvg's locked dependencies with the Go cargo.
resvg_vendor = command(
    name="resvg_vendor",
    inputs=["$(S)/tst/vendor.py"] + TESTS_LIB,
    outputs=["$(B)/tst/resvg-vendor.tar.zst"],
    cmd=[
        "python3", "$(S)/tst/vendor.py",
        "$(B)/tst/resvg-src.tar", ".",
        "$(B)/tst/resvg-vendor.tar.zst",
    ],
    deps=[resvg_src, cargo],
    env={"CARGO": "$(B)/bin/cargo"},
    descr="VN",
    color="magenta",
)

# resvg: build resvg offline against the shared libstd, then render-test it.
resvg = command(
    name="resvg",
    inputs=["$(S)/tst/build_project.py", "$(S)/tst/resvg/run.py"] + TESTS_LIB,
    outputs=["$(B)/tst/resvg.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/build_project.py",
            "$(B)/tst/resvg-src.tar",
            "$(B)/tst/resvg-vendor.tar.zst",
            "$(B)/tst/libstd.tar",
            "crates/resvg",
            "python3", "$(S)/tst/resvg/run.py", "@BIN@",
        ],
        [*TEST_TIMEOUT, "sh", "-c", "> $(B)/tst/resvg.stamp"],
    ],
    deps=[resvg_src, resvg_vendor, libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="TS",
    color="magenta",
)

# Unit regressions: one self-contained tst/unit/test_*.rs per compiler fix,
# each its own node — compiled against the shared libstd and run (must exit 0).
unit_tests = [
    command(
        name="unit_doctest_import",
        inputs=[
            "$(S)/tst/rust_doctest/import.py",
            "$(S)/tst/rust_doctest/test_import.py",
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
    name="unit_mir_opt_level",
    inputs=[
        "$(S)/tst/unit/test_mir_opt_level.py",
        "$(S)/tst/unit/mir_opt_level_input.rs",
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
    ],
    outputs=["$(B)/tst/unit/codegen_options.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tst/unit/test_codegen_options.py",
        "$(B)/bin/rustc",
        "$(S)/tst/unit/mir_opt_level_input.rs",
        "$(S)/tst/unit/codegen_options_cfg.rs",
        "$(B)/tst/unit/codegen_options.stamp",
    ],
    deps=[rustc],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_driver_lint_cfg_options",
    inputs=[
        "$(S)/tst/unit/test_driver_lint_cfg_options.py",
        "$(S)/tst/unit/driver_lint_cfg_input.rs",
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
    name="unit_compiletest_flags",
    inputs=[
        "$(S)/tst/unit/test_compiletest_flags.py",
        "$(S)/tst/lib.py",
        "$(S)/tst/rust_1_90/adapter.py",
        "$(S)/tst/rust_ui_compile/import.py",
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
for _src in build.glob("$(S)/tst/unit/test_*.rs"):
    _stem = _src.rsplit("/", 1)[1][len("test_"):-len(".rs")]
    _uses_rust_lib_dependencies = _stem == "rust_lib_dev_dependencies"
    unit_tests.append(command(
        name="unit_" + _stem,
        inputs=[_src, "$(S)/tst/unit/run_one.py"] + TESTS_LIB,
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
            **({"RUST_LIB_DEPENDENCIES": "$(B)/tst/rust-lib-dependencies.tar"}
               if _uses_rust_lib_dependencies else {}),
        },
        descr="UT",
        color="green",
    ))

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
        env={"RUSTC": "$(B)/bin/rustc"},
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
            "$(B)/tst/rust_1_90/" + _case + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/bin/rustc"},
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

# gccrs' no_core execute tests need no Rust standard library.  Preserve the
# upstream file boundary and let the adapter interpret its dg-* invariants.
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
            *TESTS_LIB,
        ],
        outputs=["$(B)/tst/gccrs/" + _case + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/gccrs/adapter.py",
            _case, _src, "$(B)/tst/gccrs/" + _case + ".stamp",
        ],
        deps=[rustc],
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
            *gccrs_compile_sources,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tst/gccrs_compile/adapter.py",
            "$(S)/tst/gccrs_compile/cases.txt", str(_index), "1",
            "$(S)/tst/gccrs_compile/upstream", _stamp,
        ],
        deps=[rustc],
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
rust_lib_tests_by_group = {}
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
            *TEST_TIMEOUT,
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
    rust_lib_tests.append(_target)
    rust_lib_tests_by_group.setdefault(_key, []).append(_target)

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
            *TEST_TIMEOUT,
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
    *unit_tests,
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


group("test", resvg, *lite_tests)
group("lite_tests", *partition_lite_tests(lite_tests))
group("unit", *unit_tests)
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
