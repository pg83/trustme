#![feature(f128)]

const SIGN: u128 = 1u128 << 127;
const EXPONENT: u128 = 0x7fffu128 << 112;
const TINY: f128 = 1.0e-1000000001f128;
const MAXIMUM: f128 = f128::from_bits((0x7ffeu128 << 112) | ((1u128 << 112) - 1));
const RESULTS: [u128; 5] = [
    TINY.to_bits(),
    (-TINY).to_bits(),
    (MAXIMUM * 2.0f128).to_bits(),
    (-1.0f128).min(0.0f128).to_bits(),
    (-1.0f128).max(0.0f128).to_bits(),
];
const NARROW: [u64; 2] = [
    (f128::from_bits(1) as f64).to_bits(),
    (f128::from_bits(SIGN | 1) as f64).to_bits(),
];

fn main() {
    assert_eq!(RESULTS, [0, SIGN, EXPONENT, (-1.0f128).to_bits(), 0]);
    assert_eq!(NARROW, [0, 1u64 << 63]);
}
