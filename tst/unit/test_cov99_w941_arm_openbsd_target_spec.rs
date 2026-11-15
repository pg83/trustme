//@ compile-only
//@ compile-flags: --target arm-unknown-openbsd --dump-target-spec $OUT.toml
//@ output-contains: .toml family = "unix"
//@ output-contains: .toml os-name = "openbsd"
//@ output-contains: .toml env-name = "gnu"
//@ output-contains: .toml target = "arm-unknown-openbsd"
//@ output-contains: .toml name = "arm"
//@ output-contains: .toml pointer-bits = 32
//@ output-contains: .toml is-big-endian = false

fn main() {}
