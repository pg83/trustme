#![feature(f128)]

const SIGN: u128 = 1u128 << 127;
const LOW: f128 = f128::from_bits(1);
const THREE_LOW: f128 = f128::from_bits(3);
const MIN_NORMAL: f128 = f128::from_bits(1u128 << 112);
const MAX_SUBNORMAL: f128 = f128::from_bits((1u128 << 112) - 1);
const BEFORE_TWO: f128 = f128::from_bits((0x4000u128 << 112) - 1);
const HALF_ULP: f128 = f128::from_bits((16383u128 - 113) << 112);

const RESULTS: [u128; 10] = [
    (LOW / 2.0f128).to_bits(),
    (THREE_LOW / 2.0f128).to_bits(),
    (-LOW / 2.0f128).to_bits(),
    (LOW * LOW).to_bits(),
    (-LOW * LOW).to_bits(),
    (BEFORE_TWO + HALF_ULP).to_bits(),
    (MIN_NORMAL * 0.5f128).to_bits(),
    (MAX_SUBNORMAL + LOW).to_bits(),
    (f64::from_bits(1) as f128).to_bits(),
    (f64::from_bits((1u64 << 63) | 1) as f128).to_bits(),
];

fn main() {
    assert_eq!(RESULTS[0], 0);
    assert_eq!(RESULTS[1], 2);
    assert_eq!(RESULTS[2], SIGN);
    assert_eq!(RESULTS[3], 0);
    assert_eq!(RESULTS[4], SIGN);
    assert_eq!(RESULTS[5], 0x4000u128 << 112);
    assert_eq!(RESULTS[6], 1u128 << 111);
    assert_eq!(RESULTS[7], 1u128 << 112);
    assert_eq!(RESULTS[8], (16383u128 - 1074) << 112);
    assert_eq!(RESULTS[9], SIGN | ((16383u128 - 1074) << 112));
}
