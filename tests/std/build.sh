#!/bin/sh
# Build the mrustc standard library (libcore/liballoc/libstd/libtest/…) plus
# libproc_macro from a packed rust-src tree, into a tar. This is the `libstd`
# graph node — built once, depended on by every project build.
#
#   build.sh <rust-src.tar> <out.tar>
#
# Environment:
#   RUSTC      the mrustc binary (any name; a `mrustc` link is made for it)
#   MINICARGO  the minicargo binary
#   CC         C compiler for the generated code
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
src_tar="$1"
out="$2"

MINICARGO="${MINICARGO:?set MINICARGO}"
: "${RUSTC:?set RUSTC}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# minicargo picks the mrustc protocol by the compiler's basename.
mkdir -p "$work/bin"
ln -sf "$(readlink -f "$RUSTC")" "$work/bin/mrustc"
export MRUSTC_PATH="$work/bin/mrustc"
export MRUSTC_TARGET_VER=1.90
export RUSTC_VERSION=1.90.0
export STD_ENV_ARCH="${STD_ENV_ARCH:-x86_64}"
export MINICARGO_DEFER_CODEGEN=0
export CC="${CC:-cc}"

src="$work/rust-src"
mkdir -p "$src"
tar -C "$src" -xf "$src_tar"

outdir="$work/libstd"
mkdir -p "$outdir"

echo "[libstd] standard library" >&2
"$MINICARGO" \
    --vendor-dir "$src/vendor" \
    --script-overrides "$here/script-overrides/stable-1.90.0-linux" \
    --manifest-overrides "$here/rustc-1.90.0-overrides.toml" \
    --output-dir "$outdir" \
    "$src/mrustc-stdlib/"

echo "[libstd] libproc_macro" >&2
"$MINICARGO" --output-dir "$outdir" "$here/libproc_macro"

echo "[libstd] packing $out" >&2
tar -C "$outdir" -cf "$out" .
