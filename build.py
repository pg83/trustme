import build

# Build description for rustc (the mrustc-derived Rust compiler).
# All sources live flat in the repo root; plain C++14, links zlib.

build.includes += ["$(S)"]

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
    "-std=c++14",
    "-O2",
    "-g",
    "-Wall",
    "-Wno-pessimizing-move",
    "-Wno-misleading-indentation",
    "-Werror=return-type",
    "-Werror=switch",
]

SRC = [
    "$(S)/ast_ast.cpp",
    "$(S)/ast_crate.cpp",
    "$(S)/ast_dump.cpp",
    "$(S)/ast_expr.cpp",
    "$(S)/ast_path.cpp",
    "$(S)/ast_pattern.cpp",
    "$(S)/ast_types.cpp",
    "$(S)/common_debug.cpp",
    "$(S)/debug.cpp",
    "$(S)/expand_asm.cpp",
    "$(S)/expand_assert.cpp",
    "$(S)/expand_cfg.cpp",
    "$(S)/expand_codegen.cpp",
    "$(S)/expand_compile_error.cpp",
    "$(S)/expand_concat.cpp",
    "$(S)/expand_crate_tags.cpp",
    "$(S)/expand_derive.cpp",
    "$(S)/expand_doc.cpp",
    "$(S)/expand_env.cpp",
    "$(S)/expand_file_line.cpp",
    "$(S)/expand_format_args.cpp",
    "$(S)/expand_include.cpp",
    "$(S)/expand_lang_item.cpp",
    "$(S)/expand_lints.cpp",
    "$(S)/expand_macro_rules.cpp",
    "$(S)/expand_misc_attrs.cpp",
    "$(S)/expand_mod.cpp",
    "$(S)/expand_panic.cpp",
    "$(S)/expand_proc_macro.cpp",
    "$(S)/expand_rustc_box.cpp",
    "$(S)/expand_rustc_diagnostics.cpp",
    "$(S)/expand_stability.cpp",
    "$(S)/expand_std_prelude.cpp",
    "$(S)/expand_stringify.cpp",
    "$(S)/expand_test.cpp",
    "$(S)/expand_test_harness.cpp",
    "$(S)/hir_conv_bind.cpp",
    "$(S)/hir_conv_constant_evaluation.cpp",
    "$(S)/hir_conv_expand_type.cpp",
    "$(S)/hir_conv_lifetime_elision.cpp",
    "$(S)/hir_conv_markings.cpp",
    "$(S)/hir_conv_resolve_ufcs.cpp",
    "$(S)/hir_crate_post_load.cpp",
    "$(S)/hir_crate_ptr.cpp",
    "$(S)/hir_deserialise.cpp",
    "$(S)/hir_dump.cpp",
    "$(S)/hir_expand_annotate_value_usage.cpp",
    "$(S)/hir_expand_closures.cpp",
    "$(S)/hir_expand_erased_types.cpp",
    "$(S)/hir_expand_lifetime_infer.cpp",
    "$(S)/hir_expand_reborrow.cpp",
    "$(S)/hir_expand_static_borrow_constants.cpp",
    "$(S)/hir_expand_ufcs_everything.cpp",
    "$(S)/hir_expand_vtable.cpp",
    "$(S)/hir_expr.cpp",
    "$(S)/hir_expr_ptr.cpp",
    "$(S)/hir_from_ast.cpp",
    "$(S)/hir_from_ast_expr.cpp",
    "$(S)/hir_generic_params.cpp",
    "$(S)/hir_hir.cpp",
    "$(S)/hir_hir_ops.cpp",
    "$(S)/hir_inherent_cache.cpp",
    "$(S)/hir_path.cpp",
    "$(S)/hir_pattern.cpp",
    "$(S)/hir_serialise.cpp",
    "$(S)/hir_serialise_lowlevel.cpp",
    "$(S)/hir_type.cpp",
    "$(S)/hir_typeck_common.cpp",
    "$(S)/hir_typeck_expr_check.cpp",
    "$(S)/hir_typeck_expr_cs.cpp",
    "$(S)/hir_typeck_expr_cs__enum.cpp",
    "$(S)/hir_typeck_expr_visit.cpp",
    "$(S)/hir_typeck_helpers.cpp",
    "$(S)/hir_typeck_impl_ref.cpp",
    "$(S)/hir_typeck_outer.cpp",
    "$(S)/hir_typeck_resolve_common.cpp",
    "$(S)/hir_typeck_static.cpp",
    "$(S)/hir_visitor.cpp",
    "$(S)/ident.cpp",
    "$(S)/jobserver.cpp",
    "$(S)/macro_rules_eval.cpp",
    "$(S)/macro_rules_mod.cpp",
    "$(S)/macro_rules_parse.cpp",
    "$(S)/main.cpp",
    "$(S)/memory_dump.cpp",
    "$(S)/mir_borrow_check.cpp",
    "$(S)/mir_check.cpp",
    "$(S)/mir_check_full.cpp",
    "$(S)/mir_cleanup.cpp",
    "$(S)/mir_dump.cpp",
    "$(S)/mir_from_hir.cpp",
    "$(S)/mir_from_hir_match.cpp",
    "$(S)/mir_helpers.cpp",
    "$(S)/mir_mir.cpp",
    "$(S)/mir_mir_builder.cpp",
    "$(S)/mir_mir_ptr.cpp",
    "$(S)/mir_optimise.cpp",
    "$(S)/mir_visit_crate_mir.cpp",
    "$(S)/parse_expr.cpp",
    "$(S)/parse_interpolated_fragment.cpp",
    "$(S)/parse_lex.cpp",
    "$(S)/parse_parseerror.cpp",
    "$(S)/parse_paths.cpp",
    "$(S)/parse_pattern.cpp",
    "$(S)/parse_root.cpp",
    "$(S)/parse_token.cpp",
    "$(S)/parse_tokenstream.cpp",
    "$(S)/parse_tokentree.cpp",
    "$(S)/parse_ttstream.cpp",
    "$(S)/parse_types.cpp",
    "$(S)/path.cpp",
    "$(S)/rc_string.cpp",
    "$(S)/resolve_absolute.cpp",
    "$(S)/resolve_common.cpp",
    "$(S)/resolve_index.cpp",
    "$(S)/resolve_use.cpp",
    "$(S)/span.cpp",
    "$(S)/toml.cpp",
    "$(S)/trans_allocator.cpp",
    "$(S)/trans_auto_impls.cpp",
    "$(S)/trans_codegen.cpp",
    "$(S)/trans_codegen_c.cpp",
    "$(S)/trans_codegen_c_structured.cpp",
    "$(S)/trans_codegen_mmir.cpp",
    "$(S)/trans_enumerate.cpp",
    "$(S)/trans_mangling_v2.cpp",
    "$(S)/trans_monomorphise.cpp",
    "$(S)/trans_target.cpp",
    "$(S)/trans_trans_list.cpp",
    "$(S)/version.cpp",
]

rustc = program(
    srcs=SRC,
    name="rustc",
    ldflags=["-lz"],
)

# minicargo: the transitional per-crate build driver. It links the same
# common support sources (now flat in the root) plus its own sources under
# tools/minicargo. Kept until the Go cargo learns to orchestrate builds.
MINICARGO = [
    "$(S)/tools/minicargo/main.cpp",
    "$(S)/tools/minicargo/manifest.cpp",
    "$(S)/tools/minicargo/repository.cpp",
    "$(S)/tools/minicargo/cfg.cpp",
    "$(S)/tools/minicargo/build.cpp",
    "$(S)/tools/minicargo/jobs.cpp",
    "$(S)/tools/minicargo/file_timestamp.cpp",
    "$(S)/tools/minicargo/os.cpp",
    "$(S)/tools/minicargo/resolve_0minicargo.cpp",
    "$(S)/tools/minicargo/resolve_cargo.cpp",
    # shared support library (flat in the root)
    "$(S)/toml.cpp",
    "$(S)/path.cpp",
    "$(S)/common_debug.cpp",
    "$(S)/jobserver.cpp",
]

minicargo = program(
    srcs=MINICARGO,
    name="minicargo",
    cppflags=["-I$(S)/tools/minicargo"],
    cxxflags=["-std=c++14"],
    ldflags=["-lz", "-pthread"],
)

# cargo: the new lockfile-driven cargo, written in Go. Building it is just a
# `go build` dropped into the graph as a command node. GOCACHE is pinned into
# the build dir so the compile is incremental and stays out of $HOME.
cargo = command(
    name="cargo",
    inputs=build.glob("$(S)/cargo/*.go") + ["$(S)/cargo/go.mod"],
    outputs=["$(B)/cargo"],
    cmd=[
        "go", "build",
        "-o", "$(B)/cargo",
        ".",
    ],
    cwd="$(S)/cargo",
    env={
        "GOCACHE": "$(B)/gocache",
        "GOFLAGS": "-mod=mod",
        "GOTOOLCHAIN": "local",
    },
    descr="GO",
)

# cargo is intentionally not install()ed: the convenience root symlink would
# collide with the cargo/ source directory. It is built as $(B)/cargo and
# referenced from there (e.g. by the test graph).
install(rustc, minicargo)


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
    "RUSTC": "$(B)/rustc",
    "MINICARGO": "$(B)/minicargo",
}

# std_src: fetch + patch the rust-1.90 source, add the shim, pack it.
std_src = command(
    name="std_src",
    inputs=["$(S)/tests/std/fetch.sh", "$(S)/tests/std/rustc-1.90.0-src.patch"],
    outputs=["$(B)/tests/rust-src.tar"],
    cmd=["$(S)/tests/std/fetch.sh", "$(B)/tests/rust-src.tar"],
    descr="RS",
    color="cyan",
)

# libstd: build the standard library (+ libproc_macro) once, from that source.
libstd = command(
    name="libstd",
    inputs=(
        ["$(S)/tests/std/build.sh", "$(S)/tests/std/rustc-1.90.0-overrides.toml"]
        + build.glob("$(S)/tests/std/script-overrides/stable-1.90.0-linux/**")
        + build.glob("$(S)/tests/std/libproc_macro/**")
    ),
    outputs=["$(B)/tests/libstd.tar"],
    cmd=["$(S)/tests/std/build.sh", "$(B)/tests/rust-src.tar", "$(B)/tests/libstd.tar"],
    deps=[std_src, rustc, minicargo],
    env=TOOLCHAIN_ENV,
    descr="LS",
    color="cyan",
)

# resvg_src: the project source at a pinned revision.
resvg_src = command(
    name="resvg_src",
    inputs=["$(S)/tests/git_src.sh"],
    outputs=["$(B)/tests/resvg-src.tar"],
    cmd=[
        "$(S)/tests/git_src.sh",
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
    inputs=["$(S)/tests/vendor.sh"],
    outputs=["$(B)/tests/resvg-vendor.tar.zst"],
    cmd=[
        "$(S)/tests/vendor.sh",
        "$(B)/tests/resvg-src.tar", ".",
        "$(B)/tests/resvg-vendor.tar.zst",
    ],
    deps=[resvg_src, cargo],
    env={"CARGO": "$(B)/cargo"},
    descr="VN",
    color="magenta",
)

# resvg: build resvg offline against the shared libstd, then render-test it.
resvg = command(
    name="resvg",
    inputs=["$(S)/tests/build_project.sh", "$(S)/tests/resvg/run.py"],
    outputs=["$(B)/tests/resvg.stamp"],
    cmd=[
        [
            "$(S)/tests/build_project.sh",
            "$(B)/tests/resvg-src.tar",
            "$(B)/tests/resvg-vendor.tar.zst",
            "$(B)/tests/libstd.tar",
            "crates/resvg",
            "python3", "$(S)/tests/resvg/run.py", "@BIN@",
        ],
        ["touch", "$(B)/tests/resvg.stamp"],
    ],
    deps=[resvg_src, resvg_vendor, libstd, rustc, minicargo],
    env=TOOLCHAIN_ENV,
    descr="TS",
    color="magenta",
)

group("test", resvg)

