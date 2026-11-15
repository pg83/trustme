//@ compile-only
//@ compile-flags: --target i686-unknown-openbsd --dump-target-spec $OUT.toml
//@ output-contains: .toml family = "unix"
//@ output-contains: .toml os-name = "openbsd"
//@ output-contains: .toml env-name = "gnu"
//@ output-contains: .toml target = "i686-unknown-openbsd"
//@ output-contains: .toml name = "x86"
//@ output-contains: .toml pointer-bits = 32
//@ output-contains: .toml is-big-endian = false

fn main() {}
