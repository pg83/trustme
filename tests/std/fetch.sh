#!/bin/sh
# Fetch the rust-1.90.0 standard-library source, patch it, drop in the
# mrustc-stdlib shim, and pack the tree into a tar. This is the `std_src`
# graph node — shared by every project test.
#
#   fetch.sh <out.tar>
#
# Set RUST_SRC to an already-unpacked rust-1.90.0-src tree to skip the download.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
out="$1"
ver="1.90.0"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
src="$work/rust-src"

if [ -n "${RUST_SRC:-}" ]; then
    cp -a "$RUST_SRC" "$src"
else
    echo "[std_src] downloading rustc-$ver-src" >&2
    curl -sSL -o "$work/src.tar.gz" \
        "https://static.rust-lang.org/dist/rustc-$ver-src.tar.gz"
    tar -C "$work" -xf "$work/src.tar.gz"
    mv "$work/rustc-$ver-src" "$src"
    ( cd "$src" && patch -p0 < "$here/rustc-$ver-src.patch" )
fi

# The shim crate pulls std + panic_unwind + test + the rustc-std-workspace-*
# crates into one minicargo build.
shim="$src/mrustc-stdlib"
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

echo "[std_src] packing $out" >&2
tar -C "$src" -cf "$out" .
