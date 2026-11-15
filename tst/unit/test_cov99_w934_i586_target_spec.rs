//@ compile-only
//@ compile-flags: --target i586-unknown-linux-gnu --dump-target-spec $OUT.toml
//@ output-contains: .toml target = "i586-linux-gnu"
//@ output-contains: .toml name = "x86"
//@ output-contains: .toml pointer-bits = 32

fn main() {}
