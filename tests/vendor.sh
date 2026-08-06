#!/bin/sh
# Vendor a project's locked dependencies into a hermetic tar.zst using the Go
# cargo.
#
#   vendor.sh <project-manifest-dir> <out.tar.zst> [keep-dir]
#
# Environment:
#   CARGO           path to the Go cargo binary
#   SSL_CERT_FILE   CA bundle, if the environment lacks system certs
set -eu

manifest_dir="$1"
out="$2"
keep="${3:-}"

CARGO="${CARGO:?set CARGO to the cargo binary}"

if [ -n "$keep" ]; then
    "$CARGO" vendor --manifest-dir "$manifest_dir" --out "$out" --keep-dir "$keep"
else
    "$CARGO" vendor --manifest-dir "$manifest_dir" --out "$out"
fi
