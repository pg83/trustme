//@ input: support/targets/queried_m68k_selector.toml
//@ stdout-flags: --target=$DIR/support/targets/queried_m68k_selector.toml --print=cfg || --target=m68k-unknown-linux-gnu --print=cfg => target_arch="m68k"
//@ stdout-flags: --target=$DIR/support/targets/queried_m68k_selector.toml --print=cfg || --target=m68k-unknown-linux-gnu --print=cfg => target_pointer_width="32"
//@ stdout-flags: --target=$DIR/support/targets/queried_m68k_selector.toml --print=cfg || --target=m68k-unknown-linux-gnu --print=cfg => target_endian="big"
fn main() {
    let original = std::hint::black_box(383u32);
    let bytes = original.to_be_bytes();
    assert_eq!(u32::from_be_bytes(bytes), original);
    assert_eq!(std::mem::size_of::<usize>(), std::mem::size_of::<*const u8>());
}
