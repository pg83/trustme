//@ input: support/targets/query_with_extension_fields.toml
//@ stdout-flags: --target=$DIR/support/targets/query_with_extension_fields.toml --print=cfg || --target=x86_64-unknown-linux-gnu --print=cfg => target_arch="x86_64"
fn main() {
    let bytes = std::hint::black_box([1u8, 0, 1, 0]);
    assert_eq!(u32::from_le_bytes(bytes), 65_537);
}
