//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#[inline(never)]
fn classify(value: u128) -> u8 {
    match value {
        0 => 3,
        17 => 5,
        0xffff_ffff_ffff_ffff => 7,
        _ => 11,
    }
}

fn main() {
    assert_eq!(classify(0), 3);
    assert_eq!(classify(17), 5);
    assert_eq!(classify(u64::MAX as u128), 7);
    assert_eq!(classify(1u128 << 64), 11);
    assert_eq!(classify((1u128 << 64) | 17), 11);
    assert_eq!(classify(u128::MAX), 11);
}
