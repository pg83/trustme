#!/usr/bin/env python3
"""Fetch one rustc release's standard-library source, adjust it, drop in the
trustme-stdlib shim, and pack the tree into a tar: the `std_src_<version>`
graph node, one per release the graph builds a standard library for.

    fetch.py <version> <out.tar>

Set RUST_SRC to an already-unpacked rustc-<version>-src tree to skip the
download.
"""
import os
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import lib  # noqa: E402

SHIM_TOML = """\
[workspace]

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


# The libc that library/Cargo.lock pins builds without its build script, as std
# does: no host std exists yet to run one. Each version gets the cfgs its
# script would have printed for the target.
LIBC_EDITS = {
    "0.2.174": (
        ("src/macros.rs", "if #[cfg(libc_const_extern_fn)] {", "if #[cfg(all())] {", 1),
    ),
    "0.2.177": (),
}


def locked_version(lockfile: Path, name: str) -> str:
    versions = re.findall(
        rf'^\[\[package\]\]\nname = "{re.escape(name)}"\nversion = "([^"]+)"$',
        lockfile.read_text(),
        re.MULTILINE,
    )
    if len(versions) != 1:
        raise RuntimeError(f"{lockfile}: expected one {name}, found {versions}")
    return versions[0]


def source_edits(src: str) -> tuple:
    libc = locked_version(Path(src) / "library" / "Cargo.lock", "libc")
    vendored = f"vendor/libc-{libc}"
    return (
        *SOURCE_EDITS,
        (f"{vendored}/Cargo.toml", 'build = "build.rs"\n', "build = false\n", 1),
        *((f"{vendored}/{relative}", old, new, expected)
          for relative, old, new, expected in LIBC_EDITS[libc]),
    )


def adjust_sources(src: str) -> None:
    root = Path(src)
    for relative, old, new, expected in source_edits(src):
        path = root / relative
        text = path.read_text()
        count = text.count(old)
        if count == expected:
            path.write_text(text.replace(old, new))
        elif count != 0 or text.count(new) < expected:
            raise RuntimeError(
                f"{relative}: expected {expected} occurrences, found {count}"
            )


def write_shim(src: str) -> None:
    """The shim pulls std + panic_unwind + test + workspace crates into one
    build. Upstream builds them in the `library/` workspace against
    `library/Cargo.lock`; the shim is a workspace of its own carrying that
    lockfile, not a stray member of the compiler's workspace above it."""
    shim = os.path.join(src, "trustme-stdlib")
    os.makedirs(shim, exist_ok=True)
    with open(os.path.join(shim, "lib.rs"), "w") as fh:
        fh.write("#![no_core]\n")
    with open(os.path.join(shim, "Cargo.toml"), "w") as fh:
        fh.write(SHIM_TOML)
    shutil.copyfile(os.path.join(src, "library", "Cargo.lock"), os.path.join(shim, "Cargo.lock"))


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: fetch.py VERSION OUT_TAR")
    version = sys.argv[1]
    out = os.path.abspath(sys.argv[2])
    with lib.workdir() as work:
        src = os.path.join(work, "rust-src")
        local = os.environ.get("RUST_SRC")
        if local:
            shutil.copytree(local, src, symlinks=True)
        else:
            lib.log(f"[std_src] downloading rustc-{version}-src")
            tarball = os.path.join(work, "src.tar.gz")
            lib.run(["curl", "-sSL", "-o", tarball,
                     f"https://static.rust-lang.org/dist/rustc-{version}-src.tar.gz"])
            lib.run(["tar", "-C", work, "-xf", tarball])
            os.rename(os.path.join(work, f"rustc-{version}-src"), src)
        adjust_sources(src)
        write_shim(src)

        lib.log(f"[std_src] packing {out}")
        lib.tar_dir(src, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
