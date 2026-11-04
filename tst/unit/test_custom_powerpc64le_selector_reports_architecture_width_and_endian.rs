//@ input: support/targets/queried_powerpc64le_selector.toml
//@ stdout-flags: --target=$DIR/support/targets/queried_powerpc64le_selector.toml --print=cfg || --target=powerpc64le-unknown-linux-gnu --print=cfg => target_arch="powerpc64"
//@ stdout-flags: --target=$DIR/support/targets/queried_powerpc64le_selector.toml --print=cfg || --target=powerpc64le-unknown-linux-gnu --print=cfg => target_pointer_width="64"
//@ stdout-flags: --target=$DIR/support/targets/queried_powerpc64le_selector.toml --print=cfg || --target=powerpc64le-unknown-linux-gnu --print=cfg => target_endian="little"
fn main() {
    let original = std::hint::black_box(407u32);
    let bytes = original.to_be_bytes();
    assert_eq!(u32::from_be_bytes(bytes), original);
    assert_eq!(std::mem::size_of::<usize>(), std::mem::size_of::<*const u8>());
}
