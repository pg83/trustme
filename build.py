import hashlib
from pathlib import Path

import build

# Build description for rustc (the mrustc-derived Rust compiler).
# The compiler sources live flat under rustc/; C++26, links the vendored
# platform library and zlib.

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
            "python3", "$(S)/tests/build_project.py",
            "$(B)/tests/resvg-src.tar",
            "$(B)/tests/resvg-vendor.tar.zst",
            "$(B)/tests/libstd.tar",
            "crates/resvg",
            "python3", "$(S)/tests/resvg/run.py", "@BIN@",
        ],
        ["sh", "-c", "> $(B)/tests/resvg.stamp"],
    ],
    deps=[resvg_src, resvg_vendor, libstd, rustc, cargo],
    env=TOOLCHAIN_ENV,
    descr="TS",
    color="magenta",
)

# Unit regressions: one self-contained tests/unit/test_*.rs per compiler fix,
# each its own node — compiled against the shared libstd and run (must exit 0).
unit_tests = []
for _src in build.glob("$(S)/tests/unit/test_*.rs"):
    _stem = _src.rsplit("/", 1)[1][len("test_"):-len(".rs")]
    unit_tests.append(command(
        name="unit_" + _stem,
        inputs=[_src, "$(S)/tests/unit/run_one.py"] + TESTS_LIB,
        outputs=["$(B)/tests/unit/" + _stem + ".stamp"],
        cmd=[
            "python3", "$(S)/tests/unit/run_one.py",
            _src, "$(B)/tests/libstd.tar",
            "$(B)/tests/unit/" + _stem + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="UT",
        color="green",
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
            "python3", "$(S)/tests/rust_1_90/adapter.py",
            _case, _src, "$(B)/tests/libstd.tar",
            "$(B)/tests/rust_1_90/" + _case + ".stamp",
        ],
        deps=[libstd, rustc],
        env={"RUSTC": "$(B)/rustc/rustc"},
        descr="RP",
        color="green",
    ))

group("test", resvg, *unit_tests, *rust_1_90_tests)
group("unit", *unit_tests)
group("rust_1_90", *rust_1_90_tests)
