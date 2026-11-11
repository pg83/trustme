#![feature(intrinsics, f128)]
#![allow(internal_features)]

#[rustc_intrinsic]
unsafe fn rintf128(value: f128) -> f128;
#[rustc_intrinsic]
unsafe fn truncf128(value: f128) -> f128;

fn main() {
    use std::hint::black_box;

    let sign = 1u128 << 127;
    let tiny = f128::from_bits(1);
    unsafe {
        for (input, expected) in [
            (0.5f128, 0),
            (-0.5f128, sign),
            (tiny, 0),
            (-tiny, sign),
            (f128::INFINITY, f128::INFINITY.to_bits()),
            (f128::NEG_INFINITY, f128::NEG_INFINITY.to_bits()),
            (f128::MAX, f128::MAX.to_bits()),
        ] {
            assert_eq!(rintf128(black_box(input)).to_bits(), expected);
        }
        for (input, expected) in [
            (2.75f128, 2.0f128),
            (-2.75f128, -2.0f128),
            (281_474_976_710_656.75f128, 281_474_976_710_656.0f128),
            (18_446_744_073_709_551_617.75f128, 18_446_744_073_709_551_617.0f128),
        ] {
            assert!(truncf128(black_box(input)) == expected);
        }
        assert_eq!(truncf128(black_box(-tiny)).to_bits(), sign);
        assert_eq!(truncf128(black_box(f128::MAX)).to_bits(), f128::MAX.to_bits());
        assert!(truncf128(black_box(f128::INFINITY)) == f128::INFINITY);
        let nan = black_box(f128::NAN);
        let rounded_nan = rintf128(nan);
        let truncated_nan = truncf128(nan);
        assert!(rounded_nan != rounded_nan);
        assert!(truncated_nan != truncated_nan);
    }
}
