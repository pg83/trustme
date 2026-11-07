#![feature(f128)]

const SIGN: u128 = 1u128 << 127;
const EXPONENT: u128 = 0x7fffu128 << 112;
const INFINITY: f128 = f128::from_bits(EXPONENT);
const NEG_INFINITY: f128 = f128::from_bits(SIGN | EXPONENT);

const RESULTS: [u128; 9] = [
    (INFINITY + 2.0f128).to_bits(),
    (2.0f128 + NEG_INFINITY).to_bits(),
    (INFINITY + INFINITY).to_bits(),
    (INFINITY + NEG_INFINITY).to_bits(),
    (INFINITY * 0.0f128).to_bits(),
    (NEG_INFINITY * 2.0f128).to_bits(),
    (INFINITY / -2.0f128).to_bits(),
    (INFINITY / INFINITY).to_bits(),
    (-3.0f128 / INFINITY).to_bits(),
];

fn main() {
    assert_eq!(RESULTS[0], EXPONENT);
    assert_eq!(RESULTS[1], SIGN | EXPONENT);
    assert_eq!(RESULTS[2], EXPONENT);
    for index in [3usize, 4, 7] {
        assert_eq!(RESULTS[index] & EXPONENT, EXPONENT);
        assert_ne!(RESULTS[index] & ((1u128 << 112) - 1), 0);
    }
    assert_eq!(RESULTS[5], SIGN | EXPONENT);
    assert_eq!(RESULTS[6], SIGN | EXPONENT);
    assert_eq!(RESULTS[8], SIGN);
}
