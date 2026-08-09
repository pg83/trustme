import hashlib
import json
from pathlib import Path

import build

# Build description for rustc (the mrustc-derived Rust compiler).
# The compiler sources live flat under rustc/; C++26, links the vendored
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

build.includes += ["$(S)/rustc"]

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
    "third_party/libstd/build.py",
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

SRC = [
    "$(S)/rustc/ast_ast.cpp",
    "$(S)/rustc/ast_crate.cpp",
    "$(S)/rustc/ast_dump.cpp",
    "$(S)/rustc/ast_expr.cpp",
    "$(S)/rustc/ast_path.cpp",
    "$(S)/rustc/ast_pattern.cpp",
    "$(S)/rustc/ast_types.cpp",
    "$(S)/rustc/common_debug.cpp",
    "$(S)/rustc/debug.cpp",
    "$(S)/rustc/expand_asm.cpp",
    "$(S)/rustc/expand_assert.cpp",
    "$(S)/rustc/expand_cfg.cpp",
    "$(S)/rustc/expand_codegen.cpp",
    "$(S)/rustc/expand_compile_error.cpp",
    "$(S)/rustc/expand_concat.cpp",
    "$(S)/rustc/expand_crate_tags.cpp",
    "$(S)/rustc/expand_derive.cpp",
    "$(S)/rustc/expand_doc.cpp",
    "$(S)/rustc/expand_env.cpp",
    "$(S)/rustc/expand_file_line.cpp",
    "$(S)/rustc/expand_format_args.cpp",
    "$(S)/rustc/expand_include.cpp",
    "$(S)/rustc/expand_lang_item.cpp",
    "$(S)/rustc/expand_lints.cpp",
    "$(S)/rustc/expand_macro_rules.cpp",
    "$(S)/rustc/expand_misc_attrs.cpp",
    "$(S)/rustc/expand_mod.cpp",
    "$(S)/rustc/expand_panic.cpp",
    "$(S)/rustc/expand_proc_macro.cpp",
    "$(S)/rustc/expand_rustc_box.cpp",
    "$(S)/rustc/expand_rustc_diagnostics.cpp",
    "$(S)/rustc/expand_stability.cpp",
    "$(S)/rustc/expand_std_prelude.cpp",
    "$(S)/rustc/expand_stringify.cpp",
    "$(S)/rustc/expand_test.cpp",
    "$(S)/rustc/expand_test_harness.cpp",
    "$(S)/rustc/hir_conv_bind.cpp",
    "$(S)/rustc/hir_conv_constant_evaluation.cpp",
    "$(S)/rustc/hir_conv_expand_type.cpp",
    "$(S)/rustc/hir_conv_lifetime_elision.cpp",
    "$(S)/rustc/hir_conv_markings.cpp",
    "$(S)/rustc/hir_conv_resolve_ufcs.cpp",
    "$(S)/rustc/hir_crate_post_load.cpp",
    "$(S)/rustc/hir_deserialise.cpp",
    "$(S)/rustc/hir_dump.cpp",
    "$(S)/rustc/hir_expand_annotate_value_usage.cpp",
    "$(S)/rustc/hir_expand_closures.cpp",
    "$(S)/rustc/hir_expand_erased_types.cpp",
    "$(S)/rustc/hir_expand_lifetime_infer.cpp",
    "$(S)/rustc/hir_expand_reborrow.cpp",
    "$(S)/rustc/hir_expand_static_borrow_constants.cpp",
    "$(S)/rustc/hir_expand_ufcs_everything.cpp",
    "$(S)/rustc/hir_expand_vtable.cpp",
    "$(S)/rustc/hir_expr.cpp",
    "$(S)/rustc/hir_expr_ptr.cpp",
    "$(S)/rustc/hir_from_ast.cpp",
    "$(S)/rustc/hir_from_ast_expr.cpp",
    "$(S)/rustc/hir_generic_params.cpp",
    "$(S)/rustc/hir_hir.cpp",
    "$(S)/rustc/hir_hir_ops.cpp",
    "$(S)/rustc/hir_inherent_cache.cpp",
    "$(S)/rustc/hir_path.cpp",
    "$(S)/rustc/hir_pattern.cpp",
    "$(S)/rustc/hir_serialise.cpp",
    "$(S)/rustc/hir_serialise_lowlevel.cpp",
    "$(S)/rustc/hir_type.cpp",
    "$(S)/rustc/hir_typeck_common.cpp",
    "$(S)/rustc/hir_typeck_expr_check.cpp",
    "$(S)/rustc/hir_typeck_expr_cs.cpp",
    "$(S)/rustc/hir_typeck_expr_cs__enum.cpp",
    "$(S)/rustc/hir_typeck_expr_visit.cpp",
    "$(S)/rustc/hir_typeck_helpers.cpp",
    "$(S)/rustc/hir_typeck_impl_ref.cpp",
    "$(S)/rustc/hir_typeck_outer.cpp",
    "$(S)/rustc/hir_typeck_resolve_common.cpp",
    "$(S)/rustc/hir_typeck_static.cpp",
    "$(S)/rustc/hir_visitor.cpp",
    "$(S)/rustc/ident.cpp",
    "$(S)/rustc/jobserver.cpp",
    "$(S)/rustc/macro_rules_eval.cpp",
    "$(S)/rustc/macro_rules_mod.cpp",
    "$(S)/rustc/macro_rules_parse.cpp",
    "$(S)/rustc/main.cpp",
    "$(S)/rustc/memory_dump.cpp",
    "$(S)/rustc/mir_borrow_check.cpp",
    "$(S)/rustc/mir_check.cpp",
    "$(S)/rustc/mir_check_full.cpp",
    "$(S)/rustc/mir_cleanup.cpp",
    "$(S)/rustc/mir_dump.cpp",
    "$(S)/rustc/mir_from_hir.cpp",
    "$(S)/rustc/mir_from_hir_match.cpp",
    "$(S)/rustc/mir_helpers.cpp",
    "$(S)/rustc/mir_mir.cpp",
    "$(S)/rustc/mir_mir_builder.cpp",
    "$(S)/rustc/mir_mir_ptr.cpp",
    "$(S)/rustc/mir_optimise.cpp",
    "$(S)/rustc/mir_visit_crate_mir.cpp",
    "$(S)/rustc/parse_expr.cpp",
    "$(S)/rustc/parse_interpolated_fragment.cpp",
    "$(S)/rustc/parse_lex.cpp",
    "$(S)/rustc/parse_parseerror.cpp",
    "$(S)/rustc/parse_paths.cpp",
    "$(S)/rustc/parse_pattern.cpp",
    "$(S)/rustc/parse_root.cpp",
    "$(S)/rustc/parse_token.cpp",
    "$(S)/rustc/parse_tokenstream.cpp",
    "$(S)/rustc/parse_tokentree.cpp",
    "$(S)/rustc/parse_ttstream.cpp",
    "$(S)/rustc/parse_types.cpp",
    "$(S)/rustc/path.cpp",
    "$(S)/rustc/rc_string.cpp",
    "$(S)/rustc/resolve_absolute.cpp",
    "$(S)/rustc/resolve_common.cpp",
    "$(S)/rustc/resolve_index.cpp",
    "$(S)/rustc/resolve_use.cpp",
    "$(S)/rustc/span.cpp",
    "$(S)/rustc/toml.cpp",
    "$(S)/rustc/trans_allocator.cpp",
    "$(S)/rustc/trans_auto_impls.cpp",
    "$(S)/rustc/trans_codegen.cpp",
    "$(S)/rustc/trans_codegen_c.cpp",
    "$(S)/rustc/trans_codegen_c_structured.cpp",
    "$(S)/rustc/trans_codegen_mmir.cpp",
    "$(S)/rustc/trans_enumerate.cpp",
    "$(S)/rustc/trans_mangling_v2.cpp",
    "$(S)/rustc/trans_monomorphise.cpp",
    "$(S)/rustc/trans_target.cpp",
    "$(S)/rustc/trans_trans_list.cpp",
    "$(S)/rustc/version.cpp",
]

rustc = program(
    srcs=SRC,
    name="rustc",
    output="$(B)/rustc/rustc",
    deps=[platform_libstd],
    ldflags=["-lz"],
)

# cargo: Cargo-compatible package resolver and mrustc build driver, written in
# Go. Dependencies are checked in under cargo/vendor, so this node is offline.
cargo = command(
    name="cargo",
    inputs=(
        build.glob("$(S)/cargo/**/*.go")
        + ["$(S)/cargo/go.mod", "$(S)/cargo/go.sum", "$(S)/cargo/vendor/modules.txt"]
    ),
    outputs=["$(B)/cargo/cargo"],
    cmd=[
        "go", "build",
        "-o", "$(B)/cargo/cargo",
        ".",
    ],
    cwd="$(S)/cargo",
    env={
        "GOCACHE": "$(B)/gocache",
        "GOFLAGS": "-mod=vendor",
        "GOTOOLCHAIN": "local",
    },
    descr="GO",
)

# rustc and cargo are intentionally not install()ed: their convenience root
# symlinks would collide with the rustc/ and cargo/ source directories. They
# are built as $(B)/rustc and $(B)/cargo and referenced from there (e.g. by the
# test graph).
# --- tests -----------------------------------------------------------------
# A test is one real project, built by our toolchain and exercised. The graph
# is tar-based: each node produces a single archive, and downstream nodes
# unpack what they need (the build engine only promotes declared file outputs).
#
# The standard library is a *shared* pair of nodes — fetched and built once,
# then depended on by every project. See tests/README.md.
#
# These are heavy (a from-scratch libstd plus a full project build) and only
# run on request: `./build test`, or a single artifact like `./build resvg`.

TOOLCHAIN_ENV = {
    "RUSTC": "$(B)/rustc/rustc",
    "CARGO": "$(B)/cargo/cargo",
}

# All node scripts are Python and share tests/lib.py.
TESTS_LIB = ["$(S)/tests/lib.py"]
# Bound the whole test node, including compilation performed by adapters.
# The build engine resolves Nix's `timeout` symlink to the multicall binary,
# so select its applet explicitly instead of relying on argv[0].
TEST_TIMEOUT = ["coreutils", "--coreutils-prog=timeout", "60s"]

# std_src: fetch + patch the rust-1.90 source, add the shim, pack it.
std_src = command(
    name="std_src",
    inputs=["$(S)/tests/std/fetch.py", "$(S)/tests/std/rustc-1.90.0-src.patch"] + TESTS_LIB,
    outputs=["$(B)/tests/rust-src.tar"],
    cmd=["python3", "$(S)/tests/std/fetch.py", "$(B)/tests/rust-src.tar"],
    descr="RS",
    color="cyan",
)

# libstd: build the standard library (+ libproc_macro) once, from that source.
libstd = command(
    name="libstd",
    inputs=(
        ["$(S)/tests/std/build.py", "$(S)/tests/std/rustc-1.90.0-overrides.toml"]
        + build.glob("$(S)/tests/std/script-overrides/stable-1.90.0-linux/*.txt")
        + build.glob("$(S)/tests/std/libproc_macro/**/*.rs")
        + build.glob("$(S)/tests/std/libproc_macro/Cargo.toml")
        + TESTS_LIB
    ),
    outputs=["$(B)/tests/libstd.tar"],
    cmd=["python3", "$(S)/tests/std/build.py", "$(B)/tests/rust-src.tar", "$(B)/tests/libstd.tar"],
    deps=[std_src, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="LS",
    color="cyan",
)

# resvg_src: the project source at a pinned revision.
resvg_src = command(
    name="resvg_src",
    inputs=["$(S)/tests/git_src.py"] + TESTS_LIB,
    outputs=["$(B)/tests/resvg-src.tar"],
    cmd=[
        "python3", "$(S)/tests/git_src.py",
        "https://github.com/linebender/resvg.git",
        "08c79a3148df4ce8ab08fca72204b142b95423dd",
        "$(B)/tests/resvg-src.tar",
    ],
    descr="RS",
    color="magenta",
)

# resvg_vendor: vendor resvg's locked dependencies with the Go cargo.
resvg_vendor = command(
    name="resvg_vendor",
    inputs=["$(S)/tests/vendor.py"] + TESTS_LIB,
    outputs=["$(B)/tests/resvg-vendor.tar.zst"],
    cmd=[
        "python3", "$(S)/tests/vendor.py",
        "$(B)/tests/resvg-src.tar", ".",
        "$(B)/tests/resvg-vendor.tar.zst",
    ],
    deps=[resvg_src, cargo],
    env={"CARGO": "$(B)/cargo/cargo"},
    descr="VN",
    color="magenta",
)

# resvg: build resvg offline against the shared libstd, then render-test it.
resvg = command(
    name="resvg",
    inputs=["$(S)/tests/build_project.py", "$(S)/tests/resvg/run.py"] + TESTS_LIB,
    outputs=["$(B)/tests/resvg.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/build_project.py",
            "$(B)/tests/resvg-src.tar",
            "$(B)/tests/resvg-vendor.tar.zst",
            "$(B)/tests/libstd.tar",
            "crates/resvg",
            "python3", "$(S)/tests/resvg/run.py", "@BIN@",
        ],
        [*TEST_TIMEOUT, "sh", "-c", "> $(B)/tests/resvg.stamp"],
    ],
    deps=[resvg_src, resvg_vendor, libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="TS",
    color="magenta",
)

# Unit regressions: one self-contained tests/unit/test_*.rs per compiler fix,
# each its own node — compiled against the shared libstd and run (must exit 0).
unit_tests = [
    command(
        name="unit_doctest_import",
        inputs=[
            "$(S)/tests/rust_doctest/import.py",
            "$(S)/tests/rust_doctest/test_import.py",
        ],
        outputs=["$(B)/tests/unit/doctest_import.stamp"],
        cmd=[
            [
                *TEST_TIMEOUT,
                "python3",
                "$(S)/tests/rust_doctest/test_import.py",
                "-v",
            ],
            [
                *TEST_TIMEOUT,
                "sh",
                "-c",
                "> $(B)/tests/unit/doctest_import.stamp",
            ],
        ],
        descr="UT",
        color="green",
    )
]
unit_tests.append(command(
    name="unit_rust_lib_import",
    inputs=[
        "$(S)/tests/rust_lib/import.py",
        "$(S)/tests/rust_lib/test_import.py",
    ],
    outputs=["$(B)/tests/unit/rust_lib_import.stamp"],
    cmd=[
        [
            *TEST_TIMEOUT,
            "python3",
            "$(S)/tests/rust_lib/test_import.py",
            "-v",
        ],
        [
            *TEST_TIMEOUT,
            "sh",
            "-c",
            "> $(B)/tests/unit/rust_lib_import.stamp",
        ],
    ],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_target_version_default",
    inputs=["$(S)/tests/unit/test_target_version_default.py"],
    outputs=["$(B)/tests/unit/target_version_default.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tests/unit/test_target_version_default.py",
        "$(B)/rustc/rustc",
        "$(B)/tests/unit/target_version_default.stamp",
    ],
    deps=[rustc],
    descr="UT",
    color="green",
))
unit_tests.append(command(
    name="unit_mir_opt_level",
    inputs=[
        "$(S)/tests/unit/test_mir_opt_level.py",
        "$(S)/tests/unit/mir_opt_level_input.rs",
    ],
    outputs=["$(B)/tests/unit/mir_opt_level.stamp"],
    cmd=[
        *TEST_TIMEOUT,
        "python3", "$(S)/tests/unit/test_mir_opt_level.py",
        "$(B)/rustc/rustc",
        "$(S)/tests/unit/mir_opt_level_input.rs",
        "$(B)/tests/unit/mir_opt_level.stamp",
    ],
    deps=[rustc],
    descr="UT",
    color="green",
))
for _src in build.glob("$(S)/tests/unit/test_*.rs"):
    _stem = _src.rsplit("/", 1)[1][len("test_"):-len(".rs")]
    unit_tests.append(command(
        name="unit_" + _stem,
        inputs=[_src, "$(S)/tests/unit/run_one.py"] + TESTS_LIB,
        outputs=["$(B)/tests/unit/" + _stem + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/unit/run_one.py",
            _src, "$(B)/tests/libstd.tar",
            "$(B)/tests/unit/" + _stem + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="UT",
        color="green",
    ))

# Compile-time performance regressions are deliberately separate from the
# normal test groups: they are valid programs, but expensive enough to run only
# when performance is being measured.
perf_tests = []
for _src in build.glob("$(S)/tests/perf/test_*.rs"):
    _stem = _src.rsplit("/", 1)[1][len("test_"):-len(".rs")]
    perf_tests.append(command(
        name="perf_" + _stem,
        inputs=[_src, "$(S)/tests/unit/run_one.py"] + TESTS_LIB,
        outputs=["$(B)/tests/perf/" + _stem + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/unit/run_one.py",
            _src, "$(B)/tests/libstd.tar",
            "$(B)/tests/perf/" + _stem + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="PF",
        color="cyan",
    ))

# Vendored Rust 1.90 run-pass tests. Keep one source file per graph node for
# now; sharding can be added later without changing the checked-in corpus.
rust_1_90_root = Path(__file__).parent / "tests" / "rust_1_90"
rust_1_90_cases = (rust_1_90_root / "cases.txt").read_text().splitlines()
rust_1_90_tests = []
for _case in rust_1_90_cases:
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _src = "$(S)/tests/rust_1_90/upstream/" + _case
    _sidecars = []
    _source_base = rust_1_90_root / "upstream" / _case[:-len(".rs")]
    for _suffix in (".run.stdout", ".run.stderr"):
        if Path(str(_source_base) + _suffix).exists():
            _sidecars.append(
                "$(S)/tests/rust_1_90/upstream/" + _case[:-len(".rs")] + _suffix
            )
    rust_1_90_tests.append(command(
        name="rust_1_90_" + _digest,
        inputs=[
            _src,
            *_sidecars,
            "$(S)/tests/rust_1_90/adapter.py",
            "$(S)/tests/rust_1_90/cases.txt",
            *TESTS_LIB,
        ],
        outputs=["$(B)/tests/rust_1_90/" + _case + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_1_90/adapter.py",
            _case, _src, "$(B)/tests/libstd.tar",
            "$(B)/tests/rust_1_90/" + _case + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="RP",
        color="green",
    ))

# Positive check-pass/build-pass cases from Rust 1.90.  check-pass is compiled
# as a library through the available full pipeline; failures stay observable.
rust_ui_compile_root = Path(__file__).parent / "tests" / "rust_ui_compile"
rust_ui_compile_cases = json.loads(
    (rust_ui_compile_root / "cases.json").read_text()
)
rust_ui_compile_tests = []
for _index, _case in enumerate(rust_ui_compile_cases):
    _path = _case["path"]
    _digest = hashlib.sha256(_path.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/rust_ui_compile/" + _digest + ".stamp"
    rust_ui_compile_tests.append(command(
        name="rust_ui_compile_" + _digest,
        inputs=[
            "$(S)/tests/rust_ui_compile/adapter.py",
            "$(S)/tests/rust_ui_compile/cases.json",
            "$(S)/tests/rust_ui_compile/upstream/" + _path,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_ui_compile/adapter.py",
            "$(S)/tests/rust_ui_compile/cases.json", str(_index), "1",
            "$(S)/tests/rust_ui_compile/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="UC",
        color="green",
    ))

# gccrs' no_core execute tests need no Rust standard library.  Preserve the
# upstream file boundary and let the adapter interpret its dg-* invariants.
gccrs_root = Path(__file__).parent / "tests" / "gccrs"
gccrs_cases = (gccrs_root / "cases.txt").read_text().splitlines()
gccrs_case_set = set(gccrs_cases)
gccrs_support = []
for _path in sorted((gccrs_root / "upstream").rglob("*")):
    _relative = _path.relative_to(gccrs_root / "upstream").as_posix()
    if _path.is_file() and _relative not in gccrs_case_set:
        gccrs_support.append("$(S)/tests/gccrs/upstream/" + _relative)

gccrs_tests = []
for _case in gccrs_cases:
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _src = "$(S)/tests/gccrs/upstream/" + _case
    gccrs_tests.append(command(
        name="gccrs_" + _digest,
        inputs=[
            _src,
            *gccrs_support,
            "$(S)/tests/gccrs/adapter.py",
            "$(S)/tests/gccrs/cases.txt",
            *TESTS_LIB,
        ],
        outputs=["$(B)/tests/gccrs/" + _case + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/gccrs/adapter.py",
            _case, _src, "$(B)/tests/gccrs/" + _case + ".stamp",
        ],
        deps=[rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="GX",
        color="green",
    ))

# Positive gccrs compile-suite inputs have no runtime contract.  Keep every
# crate root in its own node; all copied sources are inputs because a few use mod!.
gccrs_compile_root = Path(__file__).parent / "tests" / "gccrs_compile"
gccrs_compile_cases = (gccrs_compile_root / "cases.txt").read_text().splitlines()
gccrs_compile_sources = [
    "$(S)/tests/gccrs_compile/upstream/"
    + _path.relative_to(gccrs_compile_root / "upstream").as_posix()
    for _path in sorted((gccrs_compile_root / "upstream").rglob("*"))
    if _path.is_file()
]
gccrs_compile_tests = []
for _index, _case in enumerate(gccrs_compile_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/gccrs_compile/" + _digest + ".stamp"
    gccrs_compile_tests.append(command(
        name="gccrs_compile_" + _digest,
        inputs=[
            "$(S)/tests/gccrs_compile/adapter.py",
            "$(S)/tests/gccrs_compile/cases.txt",
            *gccrs_compile_sources,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/gccrs_compile/adapter.py",
            "$(S)/tests/gccrs_compile/cases.txt", str(_index), "1",
            "$(S)/tests/gccrs_compile/upstream", _stamp,
        ],
        deps=[rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="GC",
        color="green",
    ))

# Rust Quiz programs each print one documented answer.  Store the answer as a
# tiny sidecar instead of vendoring the prose explanations or upstream crate.
rust_quiz_root = Path(__file__).parent / "tests" / "rust_quiz"
rust_quiz_cases = (rust_quiz_root / "cases.txt").read_text().splitlines()
rust_quiz_tests = []
for _case in rust_quiz_cases:
    _number = _case.split("-", 1)[0]
    _src = "$(S)/tests/rust_quiz/upstream/" + _case
    _expected = _src[:-len(".rs")] + ".stdout"
    rust_quiz_tests.append(command(
        name="rust_quiz_" + _number,
        inputs=[
            _src,
            _expected,
            "$(S)/tests/rust_quiz/adapter.py",
            "$(S)/tests/rust_quiz/cases.txt",
            *TESTS_LIB,
        ],
        outputs=["$(B)/tests/rust_quiz/" + _case + ".stamp"],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_quiz/adapter.py",
            _case, _src, _expected, "$(B)/tests/libstd.tar",
            "$(B)/tests/rust_quiz/" + _case + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="RQ",
        color="green",
    ))

# Official solved Rustlings exercises retain the upstream distinction between
# normal binaries and rustc test harnesses.  Each file is one build node.
rustlings_root = Path(__file__).parent / "tests" / "rustlings"
rustlings_cases = [
    _line.split("\t")
    for _line in (rustlings_root / "cases.tsv").read_text().splitlines()
]
rustlings_tests = []
for _index, (_case, _mode) in enumerate(rustlings_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/rustlings/" + _digest + ".stamp"
    rustlings_tests.append(command(
        name="rustlings_" + _digest,
        inputs=[
            "$(S)/tests/rustlings/adapter.py",
            "$(S)/tests/rustlings/cases.tsv",
            "$(S)/tests/rustlings/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rustlings/adapter.py",
            "$(S)/tests/rustlings/cases.tsv", str(_index), "1",
            "$(S)/tests/rustlings/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="RL",
        color="green",
    ))

# Rust By Example Markdown fences that the reference Rust 1.90 compiler can
# build and run as independent programs.  Preserve each fence as one node.
rust_by_example_root = Path(__file__).parent / "tests" / "rust_by_example"
rust_by_example_cases = [
    _line.split("\t")
    for _line in (rust_by_example_root / "cases.tsv").read_text().splitlines()
]
rust_by_example_tests = []
for _index, (_case, _origin, _edition) in enumerate(rust_by_example_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/rust_by_example/" + _digest + ".stamp"
    rust_by_example_tests.append(command(
        name="rust_by_example_" + _digest,
        inputs=[
            "$(S)/tests/rust_by_example/adapter.py",
            "$(S)/tests/rust_by_example/cases.tsv",
            "$(S)/tests/rust_by_example/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_by_example/adapter.py",
            "$(S)/tests/rust_by_example/cases.tsv", str(_index), "1",
            "$(S)/tests/rust_by_example/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="BE",
        color="green",
    ))

# Source-only targets from the official Rust Book listings, reference-checked
# with Rust 1.90.  Copying each tiny source tree preserves module resolution.
rust_book_root = Path(__file__).parent / "tests" / "rust_book"
rust_book_cases = [
    _line.split("\t")
    for _line in (rust_book_root / "cases.tsv").read_text().splitlines()
]
rust_book_tests = []
for _index, (_case, _root, _mode, _edition) in enumerate(rust_book_cases):
    _case_id = "\t".join((_case, _root, _mode, _edition))
    _digest = hashlib.sha256(_case_id.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/rust_book/" + _digest + ".stamp"
    _sources = []
    for _path in sorted((rust_book_root / "upstream" / _case).rglob("*.rs")):
        _relative = _path.relative_to(rust_book_root / "upstream").as_posix()
        _sources.append("$(S)/tests/rust_book/upstream/" + _relative)
    rust_book_tests.append(command(
        name="rust_book_" + _digest,
        inputs=[
            "$(S)/tests/rust_book/adapter.py",
            "$(S)/tests/rust_book/cases.tsv",
            *_sources,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_book/adapter.py",
            "$(S)/tests/rust_book/cases.tsv", str(_index), "1",
            "$(S)/tests/rust_book/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="BK",
        color="green",
    ))

# Official Exercism solutions linked to their dependency-free integration
# tests.  The adapter runs ignored tests too; they are only progression gates.
exercism_rust_root = Path(__file__).parent / "tests" / "exercism_rust"
exercism_rust_cases = [
    _line.split("\t")
    for _line in (exercism_rust_root / "cases.tsv").read_text().splitlines()
]
exercism_rust_tests = []
for _index, (_slug, _crate, _edition, _count) in enumerate(exercism_rust_cases):
    _digest = hashlib.sha256(_slug.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/exercism_rust/" + _digest + ".stamp"
    _sources = []
    for _path in sorted((exercism_rust_root / "upstream" / _slug).rglob("*.rs")):
        _relative = _path.relative_to(exercism_rust_root / "upstream").as_posix()
        _sources.append("$(S)/tests/exercism_rust/upstream/" + _relative)
    exercism_rust_tests.append(command(
        name="exercism_rust_" + _digest,
        inputs=[
            "$(S)/tests/exercism_rust/adapter.py",
            "$(S)/tests/exercism_rust/cases.tsv",
            *_sources,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/exercism_rust/adapter.py",
            "$(S)/tests/exercism_rust/cases.tsv", str(_index), "1",
            "$(S)/tests/exercism_rust/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="EX",
        color="green",
    ))

# Code fences from The Rust Reference, including its documented pass, panic,
# no-run, and compile-fail modes.  The importer reference-checks every fence.
rust_reference_root = Path(__file__).parent / "tests" / "rust_reference"
rust_reference_cases = [
    _line.split("\t")
    for _line in (rust_reference_root / "cases.tsv").read_text().splitlines()
]
rust_reference_tests = []
for _index, (_case, _origin, _edition, _mode) in enumerate(rust_reference_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/rust_reference/" + _digest + ".stamp"
    rust_reference_tests.append(command(
        name="rust_reference_" + _digest,
        inputs=[
            "$(S)/tests/rust_reference/adapter.py",
            "$(S)/tests/rust_reference/cases.tsv",
            "$(S)/tests/rust_reference/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_reference/adapter.py",
            "$(S)/tests/rust_reference/cases.tsv", str(_index), "1",
            "$(S)/tests/rust_reference/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="RF",
        color="green",
    ))

# Self-contained Rustonomicon code fences, reference-checked with the same
# pass/compile/fail semantics as The Rust Reference.  Each fence is one node.
nomicon_root = Path(__file__).parent / "tests" / "nomicon"
nomicon_cases = [
    _line.split("\t")
    for _line in (nomicon_root / "cases.tsv").read_text().splitlines()
]
nomicon_tests = []
for _index, (_case, _origin, _edition, _mode) in enumerate(nomicon_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/nomicon/" + _digest + ".stamp"
    nomicon_tests.append(command(
        name="nomicon_" + _digest,
        inputs=[
            "$(S)/tests/nomicon/adapter.py",
            "$(S)/tests/nomicon/cases.tsv",
            "$(S)/tests/nomicon/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/nomicon/adapter.py",
            "$(S)/tests/nomicon/cases.tsv", str(_index), "1",
            "$(S)/tests/nomicon/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="NM",
        color="green",
    ))

# The std-only, standalone subset of the official Async Book.  Most of that
# book requires external executor crates; these four fences do not.
async_book_root = Path(__file__).parent / "tests" / "async_book"
async_book_cases = [
    _line.split("\t")
    for _line in (async_book_root / "cases.tsv").read_text().splitlines()
]
async_book_tests = []
for _index, (_case, _origin, _edition, _mode) in enumerate(async_book_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/async_book/" + _digest + ".stamp"
    async_book_tests.append(command(
        name="async_book_" + _digest,
        inputs=[
            "$(S)/tests/async_book/adapter.py",
            "$(S)/tests/async_book/cases.tsv",
            "$(S)/tests/async_book/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/async_book/adapter.py",
            "$(S)/tests/async_book/cases.tsv", str(_index), "1",
            "$(S)/tests/async_book/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="AB",
        color="green",
    ))

# Rust's library unit tests are grouped only at compilation.  Each explicit
# #[test] function is still a separate runtime node selected from its harness.
rust_lib_root = Path(__file__).parent / "tests" / "rust_lib"
rust_lib_groups = []
for _line in (rust_lib_root / "groups.tsv").read_text().splitlines():
    rust_lib_groups.append(_line.split("\t"))
rust_lib_cases = []
for _line in (rust_lib_root / "cases.tsv").read_text().splitlines():
    rust_lib_cases.append(_line.split("\t"))
rust_lib_sources = [
    "$(S)/tests/rust_lib/upstream/"
    + _path.relative_to(rust_lib_root / "upstream").as_posix()
    for _path in sorted((rust_lib_root / "upstream").rglob("*"))
    if _path.is_file()
]
rust_lib_adapters = [
    "$(S)/tests/rust_lib/adapter/"
    + _path.relative_to(rust_lib_root / "adapter").as_posix()
    for _path in sorted((rust_lib_root / "adapter").rglob("*"))
    if _path.is_file()
]

rust_lib_harnesses = {}
rust_lib_harness_outputs = {}
for _suite, _harness_group, _kind, _root, _edition in rust_lib_groups:
    _key = (_suite, _harness_group)
    _digest = hashlib.sha256(("/".join(_key)).encode()).hexdigest()[:12]
    _output = "$(B)/tests/rust_lib/harness/" + _suite + "/" + _harness_group
    rust_lib_harness_outputs[_key] = _output
    rust_lib_harnesses[_key] = command(
        name="rust_lib_harness_" + _digest,
        inputs=[
            *rust_lib_sources,
            *rust_lib_adapters,
            "$(S)/tests/rust_lib/compile.py",
            "$(S)/tests/rust_lib/groups.tsv",
            *TESTS_LIB,
        ],
        outputs=[_output],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_lib/compile.py",
            _suite, _harness_group, _kind, _root, _edition,
            "$(S)/tests/rust_lib/upstream", "$(B)/tests/libstd.tar", _output,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="LH",
        color="green",
    )

rust_lib_tests = []
for _suite, _harness_group, _source, _function, _hint in rust_lib_cases:
    _case = _suite + "/" + _source + "::" + _hint
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _key = (_suite, _harness_group)
    _stamp = "$(B)/tests/rust_lib/cases/" + _digest + ".stamp"
    rust_lib_tests.append(command(
        name="rust_lib_" + _digest,
        inputs=[
            "$(S)/tests/rust_lib/run.py",
            "$(S)/tests/rust_lib/cases.tsv",
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_lib/run.py",
            _case, _function, _hint, rust_lib_harness_outputs[_key], _stamp,
        ],
        deps=[rust_lib_harnesses[_key]],
        descr="LT",
        color="green",
    ))

# Runnable library documentation examples, extracted into standalone
# programs so each fence remains an independent compile-and-run node.
rust_doctest_root = Path(__file__).parent / "tests" / "rust_doctest"
rust_doctest_cases = []
for _line in (rust_doctest_root / "cases.tsv").read_text().splitlines():
    rust_doctest_cases.append(_line.split("\t"))
rust_doctests = []
for _case, _origin, _edition, _mode in rust_doctest_cases:
    _digest = hashlib.sha256((_case + "\t" + _origin).encode()).hexdigest()[:12]
    _src = "$(S)/tests/rust_doctest/upstream/" + _case
    _stamp = "$(B)/tests/rust_doctest/" + _digest + ".stamp"
    rust_doctests.append(command(
        name="rust_doctest_" + _digest,
        inputs=[
            _src,
            "$(S)/tests/rust_doctest/adapter.py",
            "$(S)/tests/rust_doctest/cases.tsv",
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rust_doctest/adapter.py",
            _origin, _src, _edition, _mode, "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="DT",
        color="green",
    ))

# Fixed RustSmith programs and rustc 1.90 stdout oracles.  Keep each source and
# its oracle in a separate node so failures cannot hide later cases.
rustsmith_root = Path(__file__).parent / "tests" / "rustsmith"
rustsmith_cases = [
    _line.split("\t")
    for _line in (rustsmith_root / "cases.tsv").read_text().splitlines()
]
rustsmith_tests = []
for _index, (_stem, _seed) in enumerate(rustsmith_cases):
    _stamp = "$(B)/tests/rustsmith/" + _stem + ".stamp"
    _inputs = [
        "$(S)/tests/rustsmith/upstream/" + _stem + _suffix
        for _suffix in (".rs", ".args", ".stdout")
    ]
    rustsmith_tests.append(command(
        name="rustsmith_" + _stem,
        inputs=[
            "$(S)/tests/rustsmith/adapter.py",
            "$(S)/tests/rustsmith/cases.tsv",
            *_inputs,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/rustsmith/adapter.py",
            "$(S)/tests/rustsmith/cases.tsv", str(_index), "1",
            "$(S)/tests/rustsmith/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="RS",
        color="green",
    ))

# Self-contained native Miri pass programs from Rust 1.90.  Every upstream
# source is a separate graph node.
miri_root = Path(__file__).parent / "tests" / "miri"
miri_cases = (miri_root / "cases.tsv").read_text().splitlines()
miri_tests = []
for _index, _case in enumerate(miri_cases):
    _digest = hashlib.sha256(_case.encode()).hexdigest()[:12]
    _stamp = "$(B)/tests/miri/" + _digest + ".stamp"
    miri_tests.append(command(
        name="miri_" + _digest,
        inputs=[
            "$(S)/tests/miri/adapter.py",
            "$(S)/tests/miri/cases.tsv",
            "$(S)/tests/miri/upstream/" + _case,
            *TESTS_LIB,
        ],
        outputs=[_stamp],
        cmd=[
            *TEST_TIMEOUT,
            "python3", "$(S)/tests/miri/adapter.py",
            "$(S)/tests/miri/cases.tsv", str(_index), "1",
            "$(S)/tests/miri/upstream", "$(B)/tests/libstd.tar", _stamp,
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
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
group("rust_doctest", *rust_doctests)
group("rustsmith", *rustsmith_tests)
group("miri", *miri_tests)
