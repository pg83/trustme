#!/bin/sh
# Build one cargo project offline with our toolchain and run its test.
#
#   build_project.sh <project-manifest-dir> <vendor-dir> <libstd-dir> <out-dir> <test-cmd...>
#
# Environment:
#   RUSTC     path to the mrustc binary (named `mrustc`)
#   MINICARGO path to the minicargo binary
#   CC        C compiler for generated code
set -eu

manifest_dir="$1"; shift
vendor_dir="$1"; shift
libstd_dir="$1"; shift
out="$1"; shift
# remaining args: the test command, with @BIN@ replaced by the built binary

MINICARGO="${MINICARGO:?set MINICARGO}"
export MRUSTC_PATH="${RUSTC:?set RUSTC to the mrustc binary (named mrustc)}"
export MRUSTC_TARGET_VER=1.90
export MINICARGO_DEFER_CODEGEN=0
export CC="${CC:-cc}"

mkdir -p "$out"

echo "[build] $manifest_dir" >&2
( cd "$manifest_dir" && "$MINICARGO" . \
    --vendor-dir "$vendor_dir" \
    -L "$libstd_dir" \
    --output-dir "$out" )

# The project's binary name is the crate name; the caller passes the test
# command with @BIN@ standing in for the built executable.
bin="$out/$(basename "$manifest_dir")"
if [ ! -x "$bin" ]; then
    # fall back to the first executable in the output dir
    bin="$(find "$out" -maxdepth 1 -type f -perm -u+x | head -n1)"
fi

echo "[test] $bin" >&2
cmd=""
for a in "$@"; do
    case "$a" in
        @BIN@) cmd="$cmd \"$bin\"" ;;
        *) cmd="$cmd \"$a\"" ;;
    esac
done
eval "$cmd"
