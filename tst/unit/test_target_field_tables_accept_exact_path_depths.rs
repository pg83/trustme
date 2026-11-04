//@ input: support/targets/exact_depth_target_fields.toml
//@ stdout-flags: --target=$DIR/support/targets/exact_depth_target_fields.toml --print=cfg || --print=cfg => target_arch="x86_64"
fn main() {
    let values = std::hint::black_box([29u32, 31, 37]);
    assert_eq!(values.into_iter().fold(0, |sum, value| sum + value), 97);
}
