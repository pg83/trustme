//@ compile-only
//@ compile-flags: --target aarch64-unknown-openbsd --dump-target-spec $OUT.toml
//@ output-contains: .toml target = "aarch64-unknown-openbsd"
//@ output-contains: .toml os-name = "openbsd"
//@ output-contains: .toml name = "aarch64"

fn main() {}
