#!/bin/sh
# Fetch a project's source at a pinned revision and pack it into a tar. This is
# a `<proj>_src` graph node.
#
#   git_src.sh <url> <rev> <out.tar>
#
# Set <PROJ>_SRC (an env var named by the caller) to a local checkout to skip
# the clone — passed through as $SRC_OVERRIDE.
set -eu

url="$1"
rev="$2"
out="$3"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
src="$work/src"

if [ -n "${SRC_OVERRIDE:-}" ]; then
    cp -a "$SRC_OVERRIDE" "$src"
else
    echo "[src] cloning $url @ $rev" >&2
    git clone --no-checkout "$url" "$src"
    ( cd "$src" && git checkout -q "$rev" )
fi

tar -C "$src" -cf "$out" .
