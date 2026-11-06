#![feature(f128)]

const SIGN: u128 = 1u128 << 127;
const MIN_NORMAL: u128 = 1u128 << 112;
const RESULTS: [u128; 4] = [
    (f128::from_bits(37) % f128::from_bits(5)).to_bits(),
    (f128::from_bits(SIGN | 37) % f128::from_bits(5)).to_bits(),
    (f128::from_bits(MIN_NORMAL | 37) % f128::from_bits(MIN_NORMAL)).to_bits(),
    (f128::from_bits(SIGN | MIN_NORMAL | 37) % f128::from_bits(MIN_NORMAL)).to_bits(),
];

fn main() {
    assert_eq!(RESULTS, [2, SIGN | 2, 37, SIGN | 37]);
}
