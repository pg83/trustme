//@ compile-only
//@ compile-flags: --target arm-unknown-freebsd --dump-target-spec $OUT.toml
//@ output-contains: .toml family = "unix"
//@ output-contains: .toml os-name = "freebsd"
//@ output-contains: .toml env-name = "gnu"
//@ output-contains: .toml target = "arm-unknown-freebsd"
//@ output-contains: .toml name = "arm"
//@ output-contains: .toml pointer-bits = 32

fn main() {}
