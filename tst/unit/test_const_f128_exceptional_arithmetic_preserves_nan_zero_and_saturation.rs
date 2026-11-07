#![feature(f128)]

const SIGN: u128 = 1u128 << 127;
const EXPONENT: u128 = 0x7fffu128 << 112;
const INFINITY: f128 = f128::from_bits(EXPONENT);
const NEG_INFINITY: f128 = f128::from_bits(SIGN | EXPONENT);
const NAN: f128 = f128::from_bits(EXPONENT | (1u128 << 111));
const NAN_RESULTS: [u128; 6] = [
    (NAN + 2.0f128).to_bits(),
    (NAN * 2.0f128).to_bits(),
    (NAN / 2.0f128).to_bits(),
    (NAN % 2.0f128).to_bits(),
    (INFINITY % 2.0f128).to_bits(),
    (2.0f128 % 0.0f128).to_bits(),
];
const FINITE_RESULTS: [u128; 7] = [
    (3.0f128 / -0.0f128).to_bits(),
    (-0.0f128 / 3.0f128).to_bits(),
    (-0.0f128 % 3.0f128).to_bits(),
    (-2.0f128 % INFINITY).to_bits(),
    (1.5f128 % 2.0f128).to_bits(),
    2.0f128.min(2.0f128).to_bits(),
    (-2.0f128).max(-2.0f128).to_bits(),
];
const CASTS: [u64; 4] = [
    INFINITY as u64,
    NEG_INFINITY as u64,
    -0.75f128 as u64,
    1.0e30f128 as u64,
];
const ORDER: [bool; 3] = [NEG_INFINITY < INFINITY, INFINITY < NEG_INFINITY, -1.0f128 < 0.0f128];

fn main() {
    for bits in NAN_RESULTS {
        assert_eq!(bits & EXPONENT, EXPONENT);
        assert_ne!(bits & ((1u128 << 112) - 1), 0);
    }
    assert_eq!(FINITE_RESULTS, [
        SIGN | EXPONENT, SIGN, SIGN,
        (-2.0f128).to_bits(), 1.5f128.to_bits(),
        2.0f128.to_bits(), (-2.0f128).to_bits(),
    ]);
    assert_eq!(CASTS, [u64::MAX, 0, 0, u64::MAX]);
    assert_eq!(ORDER, [true, false, true]);
}
