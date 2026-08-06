#!/bin/sh
# Vendor a project's locked dependencies into a hermetic tar.zst with the Go
# cargo. This is a `<proj>_vendor` graph node.
#
#   vendor.sh <project-src.tar> <manifest-subdir> <out.tar.zst>
#
# Environment:
#   CARGO           the Go cargo binary
#   SSL_CERT_FILE   CA bundle, if the environment lacks system certs
set -eu

src_tar="$1"
subdir="$2"       # dir within the source tree holding Cargo.lock (may be ".")
out="$3"

CARGO="${CARGO:?set CARGO}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
src="$work/src"
mkdir -p "$src"
tar -C "$src" -xf "$src_tar"

"$CARGO" vendor --manifest-dir "$src/$subdir" --out "$out"
