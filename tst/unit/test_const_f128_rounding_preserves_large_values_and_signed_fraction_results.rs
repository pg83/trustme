#![feature(f128)]

const LARGE_BITS: u128 = (16383u128 + 120) << 112;
const LARGE: f128 = f128::from_bits(LARGE_BITS);
const RESULTS: [u128; 13] = [
    LARGE.floor().to_bits(),
    LARGE.ceil().to_bits(),
    LARGE.round_ties_even().to_bits(),
    (-2.0f128).floor().to_bits(),
    0.25f128.round().to_bits(),
    (-0.75f128).round().to_bits(),
    (-0.25f128).round_ties_even().to_bits(),
    (-0.75f128).round_ties_even().to_bits(),
    2.25f128.round_ties_even().to_bits(),
    2.75f128.round_ties_even().to_bits(),
    (-2.5f128).round_ties_even().to_bits(),
    3.5f128.round_ties_even().to_bits(),
    2.25f128.ceil().to_bits(),
];

fn main() {
    assert_eq!(RESULTS, [
        LARGE_BITS, LARGE_BITS, LARGE_BITS,
        (-2.0f128).to_bits(), 0, (-1.0f128).to_bits(),
        1u128 << 127, (-1.0f128).to_bits(),
        2.0f128.to_bits(), 3.0f128.to_bits(),
        (-2.0f128).to_bits(), 4.0f128.to_bits(), 3.0f128.to_bits(),
    ]);
}
