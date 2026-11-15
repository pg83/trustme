//@ compile-only
//@ compile-flags: --target powerpc64le-unknown-linux-gnu --dump-target-spec $OUT.toml
//@ output-contains: .toml target = "powerpc64le-unknown-linux-gnu"
//@ output-contains: .toml name = "powerpc64"
//@ output-contains: .toml pointer-bits = 64
//@ output-contains: .toml is-big-endian = false

fn main() {}
