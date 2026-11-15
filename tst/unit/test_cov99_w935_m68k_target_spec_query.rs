//@ compile-only
//@ compile-flags: --target m68k-unknown-linux-gnu --dump-target-spec $OUT.toml
//@ output-contains: .toml target = "m68k-linux-gnu"
//@ output-contains: .toml name = "m68k"
//@ output-contains: .toml pointer-bits = 32
//@ output-contains: .toml is-big-endian = true

fn main() {}
