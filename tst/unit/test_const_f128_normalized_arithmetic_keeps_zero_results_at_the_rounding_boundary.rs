#![feature(f128)]

const SIGN: u128 = 1u128 << 127;
const LOW: f128 = f128::from_bits(3);
const OTHER_LOW: f128 = f128::from_bits(5);
const RESULTS: [u128; 6] = [
    (1.25f128 + 1.5f128).to_bits(),
    (1.25f128 * 1.5f128).to_bits(),
    (5.5f128 % 2.0f128).to_bits(),
    (3.0f128 - 3.0f128).to_bits(),
    (LOW * OTHER_LOW).to_bits(),
    (-LOW * OTHER_LOW).to_bits(),
];

fn main() {
    assert_eq!(RESULTS, [
        2.75f128.to_bits(), 1.875f128.to_bits(), 1.5f128.to_bits(),
        0, 0, SIGN,
    ]);
}
