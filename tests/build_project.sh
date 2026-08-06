#!/bin/sh
# Build one project offline against a prebuilt libstd and run its test. This is
# a `<proj>` graph node (the plan's node 2: "builds and runs the tests"). It
# depends on the shared libstd tar, never rebuilding it.
#
#   build_project.sh <src.tar> <vendor.tar.zst> <libstd.tar> <manifest-subdir> <test-cmd...>
#
# The test command runs with @BIN@ replaced by the freshly built executable.
#
# Environment:
#   RUSTC      the mrustc binary (any name; a `mrustc` link is made)
#   MINICARGO  the minicargo binary
#   CC         C compiler for the generated code
set -eu

src_tar="$1"; shift
vendor_tar="$1"; shift
libstd_tar="$1"; shift
subdir="$1"; shift
# remaining args: the test command with @BIN@ placeholder

MINICARGO="${MINICARGO:?set MINICARGO}"
: "${RUSTC:?set RUSTC}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/bin"
ln -sf "$(readlink -f "$RUSTC")" "$work/bin/mrustc"
export MRUSTC_PATH="$work/bin/mrustc"
export MRUSTC_TARGET_VER=1.90
export MINICARGO_DEFER_CODEGEN=0
export CC="${CC:-cc}"

src="$work/src";     mkdir -p "$src";     tar -C "$src" -xf "$src_tar"
libstd="$work/libstd"; mkdir -p "$libstd"; tar -C "$libstd" -xf "$libstd_tar"
vroot="$work/vendor"; mkdir -p "$vroot";  tar -C "$vroot" --zstd -xf "$vendor_tar"
out="$work/out";     mkdir -p "$out"

echo "[build] $subdir" >&2
( cd "$src/$subdir" && "$MINICARGO" . \
    --vendor-dir "$vroot/vendor" \
    -L "$libstd" \
    --output-dir "$out" )

bin="$out/$(basename "$subdir")"
[ -x "$bin" ] || bin="$(find "$out" -maxdepth 1 -type f -perm -u+x | head -n1)"

echo "[test] $bin" >&2
cmd=""
for a in "$@"; do
    case "$a" in
        @BIN@) cmd="$cmd \"$bin\"" ;;
        *)     cmd="$cmd \"$a\"" ;;
    esac
done
eval "$cmd"
