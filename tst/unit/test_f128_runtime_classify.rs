#![feature(f128)]

use std::num::FpCategory;

#[inline(never)]
fn classify(value: f128) -> FpCategory {
    value.classify()
}

#[inline(never)]
fn bits(value: f128) -> u128 {
    value.to_bits()
}

fn main() {
    assert_eq!(bits(f128::NAN), 0x7fff_8000_0000_0000_0000_0000_0000_0000);
    assert_eq!(bits(f128::INFINITY), 0x7fff_0000_0000_0000_0000_0000_0000_0000);
    assert_eq!(bits(1.0f128), 0x3fff_0000_0000_0000_0000_0000_0000_0000);

    assert_eq!(classify(f128::NAN), FpCategory::Nan);
    assert_eq!(classify(f128::INFINITY), FpCategory::Infinite);
    assert_eq!(classify(0.0f128), FpCategory::Zero);
    assert_eq!(classify(f128::from_bits(1)), FpCategory::Subnormal);
    assert_eq!(classify(1.0f128), FpCategory::Normal);
}
