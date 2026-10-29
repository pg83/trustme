#![feature(f128)]
fn main() {
    let positive = std::hint::black_box(3.5f128);
    let negative = -positive;
    assert_eq!(negative, -3.5f128);
    let zero = -std::hint::black_box(0.0f128);
    assert_eq!(zero.to_bits(), 1u128 << 127);
    assert_eq!((-negative).to_bits(), positive.to_bits());
}
