#![feature(f128)]

const MIN_IS_NORMAL: bool = f128::MIN_POSITIVE.is_normal();
const MIN_BITS: u128 = f128::MIN_POSITIVE.to_bits();

fn main() {
    assert_eq!(MIN_BITS, 1u128 << 112);
    assert!(MIN_IS_NORMAL);
}
