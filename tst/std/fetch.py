#!/usr/bin/env python3
"""Fetch the rust-1.90.0 standard-library source, adjust it, drop in the
trustme-stdlib shim, and pack the tree into a tar. This is the `std_src` graph
node — shared by every project test.

    fetch.py <out.tar>

Set RUST_SRC to an already-unpacked rust-1.90.0-src tree to skip the download.
"""
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import lib  # noqa: E402

VER = "1.90.0"

SHIM_TOML = """\
[package]
name = "trustme_standard_library"
version = "0.0.0"
[lib]
path = "lib.rs"
[dependencies]
std = { path = "../library/std" }
panic_unwind = { path = "../library/panic_unwind" }
test = { path = "../library/test" }
rustc-std-workspace-core = { path = "../library/rustc-std-workspace-core" }
rustc-std-workspace-alloc = { path = "../library/rustc-std-workspace-alloc" }
rustc-std-workspace-std = { path = "../library/rustc-std-workspace-std" }
"""

SOURCE_EDITS = (
    (
        "compiler/rustc_hir/src/hir.rs",
        "// Some nodes are used a lot. Make sure they don't unintentionally get bigger.\n"
        '#[cfg(target_pointer_width = "64")]\n',
        "// Some nodes are used a lot. Make sure they don't unintentionally get bigger.\n"
        '#[cfg(not(rust_compiler="trustme"))]\n'
        '#[cfg(target_pointer_width = "64")]\n',
        1,
    ),
    (
        "compiler/rustc_errors/src/lib.rs",
        "rustc_data_structures::static_assert_size!(PResult<'_, ()>, 24);\n"
        '#[cfg(target_pointer_width = "64")]\n'
        "rustc_data_structures::static_assert_size!(PResult<'_, bool>, 24);\n",
        "rustc_data_structures::static_assert_size!(PResult<'_, ()>, 24);\n"
        '#[cfg(not(rust_compiler="trustme"))]\n'
        '#[cfg(target_pointer_width = "64")]\n'
        "rustc_data_structures::static_assert_size!(PResult<'_, bool>, 24);\n",
        1,
    ),
    (
        "compiler/rustc_parse/src/parser/mod.rs",
        "// though, because `TokenTypeSet(u128)` alignment varies on others, changing the total size.\n"
        '#[cfg(all(target_pointer_width = "64", any(target_arch = "aarch64", target_arch = "x86_64")))]\n',
        "// though, because `TokenTypeSet(u128)` alignment varies on others, changing the total size.\n"
        '#[cfg(not(rust_compiler="trustme"))]\n'
        '#[cfg(all(target_pointer_width = "64", any(target_arch = "aarch64", target_arch = "x86_64")))]\n',
        1,
    ),
    (
        "compiler/rustc_middle/src/ty/sty.rs",
        "        self.split_last().unwrap().1\n",
        "        (**self).split_last().unwrap().1\n",
        1,
    ),
    (
        "compiler/rustc_middle/src/ty/generic_args.rs",
        "        walk_visitable_list!(visitor, self.iter());\n",
        "        walk_visitable_list!(visitor, (***self).iter());\n",
        1,
    ),
    (
        "library/rustc-std-workspace-core/Cargo.toml",
        '  "compiler-builtins",\n] }\n',
        '  "compiler-builtins",\n  "no-asm",\n] }\n',
        1,
    ),
    (
        "library/proc_macro/Cargo.toml",
        '[dependencies]\nstd = { path = "../std" }\n'
        "# Workaround: when documenting this crate rustdoc will try to load crate named\n"
        "# `core` when resolving doc links. Without this line a different `core` will be\n"
        "# loaded from sysroot causing duplicate lang items and other similar errors.\n"
        'core = { path = "../core" }\n'
        'rustc-literal-escaper = { version = "0.0.5", features = ["rustc-dep-of-std"] }\n',
        "[dependencies]\n"
        'rustc-literal-escaper = { version = "0.0.5", features = ["rustc-dep-of-std"] }\n',
        1,
    ),
    (
        "library/compiler-builtins/compiler-builtins/Cargo.toml",
        '[package]\nname = "compiler_builtins"\n',
        '[package]\nbuild = false\nname = "compiler_builtins"\n',
        1,
    ),
    (
        "library/compiler-builtins/compiler-builtins/src/mem/impls.rs",
        'feature = "mem-unaligned"',
        "all()",
        8,
    ),
    (
        "vendor/libc-0.2.174/Cargo.toml",
        'build = "build.rs"\n',
        "build = false\n",
        1,
    ),
    (
        "vendor/libc-0.2.174/src/macros.rs",
        "if #[cfg(libc_const_extern_fn)] {",
        "if #[cfg(all())] {",
        1,
    ),
    (
        "library/std/Cargo.toml",
        '[package]\nname = "std"\n',
        '[package]\nbuild = false\nname = "std"\n',
        1,
    ),
    (
        "library/backtrace/src/lib.rs",
        "backtrace_in_libstd",
        "all()",
        1,
    ),
    (
        "library/backtrace/src/symbolize/mod.rs",
        "backtrace_in_libstd",
        "all()",
        1,
    ),
    (
        "library/backtrace/src/symbolize/gimli.rs",
        "backtrace_in_libstd",
        "all()",
        4,
    ),
)


def adjust_sources(src: str) -> None:
    root = Path(src)
    for relative, old, new, expected in SOURCE_EDITS:
        path = root / relative
        text = path.read_text()
        count = text.count(old)
        if count == expected:
            path.write_text(text.replace(old, new))
        elif count != 0 or text.count(new) < expected:
            raise RuntimeError(
                f"{relative}: expected {expected} occurrences, found {count}"
            )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: fetch.py OUT_TAR")
    out = os.path.abspath(sys.argv[1])
    with lib.workdir() as work:
        src = os.path.join(work, "rust-src")
        local = os.environ.get("RUST_SRC")
        if local:
            shutil.copytree(local, src, symlinks=True)
        else:
            lib.log(f"[std_src] downloading rustc-{VER}-src")
            tarball = os.path.join(work, "src.tar.gz")
            lib.run(["curl", "-sSL", "-o", tarball,
                     f"https://static.rust-lang.org/dist/rustc-{VER}-src.tar.gz"])
            lib.run(["tar", "-C", work, "-xf", tarball])
            os.rename(os.path.join(work, f"rustc-{VER}-src"), src)
        adjust_sources(src)
        # The shim pulls std + panic_unwind + test + workspace crates into one build.
        shim = os.path.join(src, "trustme-stdlib")
        os.makedirs(shim, exist_ok=True)
        with open(os.path.join(shim, "lib.rs"), "w") as fh:
            fh.write("#![no_core]\n")
        with open(os.path.join(shim, "Cargo.toml"), "w") as fh:
            fh.write(SHIM_TOML)

        lib.log(f"[std_src] packing {out}")
        lib.tar_dir(src, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
