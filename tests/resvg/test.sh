#!/bin/sh
# The resvg test: vendor its deps, build it offline against a from-source
# libstd, and render a reference SVG. This is the driver behind the
# `resvg_test` build node; each stage is a helper under tests/.
#
#   test.sh <build-dir>
#
# Environment (paths to the built toolchain, normally supplied by build.py):
#   RUSTC       mrustc binary, named `mrustc`
#   MINICARGO   minicargo binary
#   CARGO       Go cargo binary
# Source inputs (fetched into <build-dir> if unset):
#   RUST_SRC    a rust-1.90.0 source tree (library/ + vendor/)
#   RESVG_SRC   a resvg checkout (pinned revision below)
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
tests="$(dirname "$here")"
bdir="$1"
mkdir -p "$bdir"

RESVG_REV="08c79a3148df4ce8ab08fca72204b142b95423dd"
RUST_VERSION="1.90.0"

: "${RUSTC:?}" "${MINICARGO:?}" "${CARGO:?}"

# minicargo selects the mrustc protocol by the compiler's basename, so give it
# a link named `mrustc` regardless of what our binary is called.
if [ "$(basename "$RUSTC")" != "mrustc" ]; then
    mkdir -p "$bdir/bin"
    ln -sf "$(readlink -f "$RUSTC")" "$bdir/bin/mrustc"
    RUSTC="$bdir/bin/mrustc"
fi
export RUSTC MINICARGO CARGO

# --- resolve source inputs -------------------------------------------------
resvg_src="${RESVG_SRC:-}"
if [ -z "$resvg_src" ]; then
    resvg_src="$bdir/resvg-src"
    if [ ! -d "$resvg_src/.git" ]; then
        echo "[resvg] cloning $RESVG_REV" >&2
        git clone --no-checkout https://github.com/linebender/resvg.git "$resvg_src"
        ( cd "$resvg_src" && git checkout -q "$RESVG_REV" )
    fi
fi

rust_src="${RUST_SRC:-}"
if [ -z "$rust_src" ]; then
    rust_src="$bdir/rust-$RUST_VERSION-src"
    if [ ! -d "$rust_src/library/std" ]; then
        echo "[std] fetching rust-$RUST_VERSION source" >&2
        tarball="$bdir/rustc-$RUST_VERSION-src.tar.gz"
        curl -sSL -o "$tarball" \
            "https://static.rust-lang.org/dist/rustc-$RUST_VERSION-src.tar.gz"
        tar -C "$bdir" -xf "$tarball"
        ( cd "$bdir/rustc-$RUST_VERSION-src" && patch -p0 < "$tests/std/rustc-$RUST_VERSION-src.patch" )
        mv "$bdir/rustc-$RUST_VERSION-src" "$rust_src"
    fi
fi

# --- stage 1: libstd from source ------------------------------------------
libstd="$bdir/libstd"
if [ ! -f "$libstd/libstd.rlib" ]; then
    "$tests/std/build.sh" "$rust_src" "$libstd"
fi

# --- stage 2: vendor resvg's dependencies ---------------------------------
vendor="$bdir/vendor"
if [ ! -d "$vendor/vendor" ]; then
    "$tests/vendor.sh" "$resvg_src" "$bdir/resvg-vendor.tar.zst" "$vendor"
fi

# --- stage 3: build resvg + render test -----------------------------------
"$tests/build_project.sh" \
    "$resvg_src/crates/resvg" \
    "$vendor/vendor" \
    "$libstd" \
    "$bdir/resvg-out" \
    python3 "$here/run.py" @BIN@
