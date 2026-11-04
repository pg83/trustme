//@ input: support/targets/queried_riscv64_selector.toml
//@ stdout-flags: --target=$DIR/support/targets/queried_riscv64_selector.toml --print=cfg || --target=riscv64gc-unknown-linux-gnu --print=cfg => target_arch="riscv64"
//@ stdout-flags: --target=$DIR/support/targets/queried_riscv64_selector.toml --print=cfg || --target=riscv64gc-unknown-linux-gnu --print=cfg => target_pointer_width="64"
//@ stdout-flags: --target=$DIR/support/targets/queried_riscv64_selector.toml --print=cfg || --target=riscv64gc-unknown-linux-gnu --print=cfg => target_endian="little"
fn main() {
    let original = std::hint::black_box(415u32);
    let bytes = original.to_be_bytes();
    assert_eq!(u32::from_be_bytes(bytes), original);
    assert_eq!(std::mem::size_of::<usize>(), std::mem::size_of::<*const u8>());
}
