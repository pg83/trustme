//@ compile-only
//@ compile-flags: --target riscv64-unknown-linux-musl --dump-target-spec $OUT.toml
//@ output-contains: .toml family = "unix"
//@ output-contains: .toml os-name = "linux"
//@ output-contains: .toml env-name = "musl"
//@ output-contains: .toml target = "riscv64-unknown-linux-musl"
//@ output-contains: .toml name = "riscv64"
//@ output-contains: .toml pointer-bits = 64
//@ output-contains: .toml is-big-endian = false

fn main() {}
