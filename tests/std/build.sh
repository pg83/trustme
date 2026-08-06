#!/bin/sh
# Build the mrustc standard library (libcore/liballoc/libstd/libtest/…) and
# libproc_macro from a rust-1.90.0 source tree, into an output directory.
#
#   build.sh <rust-src-dir> <out-dir>
#
# Environment:
#   RUSTC     path to the rustc (mrustc) binary  (must be named `mrustc` for
#             minicargo to select the mrustc protocol — pass a symlink)
#   MINICARGO path to the minicargo binary
#   CC        C compiler for the generated code (defaults to the env's cc)
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
rust_src="$1"
out="$2"

MINICARGO="${MINICARGO:?set MINICARGO to the minicargo binary}"
export MRUSTC_PATH="${RUSTC:?set RUSTC to the mrustc binary (named mrustc)}"
export MRUSTC_TARGET_VER=1.90
export RUSTC_VERSION=1.90.0
export STD_ENV_ARCH="${STD_ENV_ARCH:-x86_64}"
export MINICARGO_DEFER_CODEGEN=0
export CC="${CC:-cc}"

mkdir -p "$out"

# The `mrustc-stdlib` shim crate pulls std + panic_unwind + test + the
# rustc-std-workspace-* crates into one build so a single minicargo invocation
# emits the whole set.
shim="$rust_src/mrustc-stdlib"
mkdir -p "$shim"
echo '#![no_core]' > "$shim/lib.rs"
cat > "$shim/Cargo.toml" <<EOF
[package]
name = "mrustc_standard_library"
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
EOF

echo "[std] building the standard library" >&2
"$MINICARGO" \
    --vendor-dir "$rust_src/vendor" \
    --script-overrides "$here/script-overrides/stable-1.90.0-linux" \
    --manifest-overrides "$here/rustc-1.90.0-overrides.toml" \
    --output-dir "$out" \
    "$shim/"

echo "[std] building libproc_macro" >&2
"$MINICARGO" --output-dir "$out" "$here/libproc_macro"

echo "[std] done -> $out" >&2
