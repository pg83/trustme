//@ compile-variant: -O
const fn signed(value: i128) -> (f32, f64) { (value as f32, value as f64) }
const fn unsigned(value: u128) -> (f32, f64) { (value as f32, value as f64) }
const fn narrowed(value: f64) -> (f32, f64) { let single = value as f32; (single, single as f64) }
const ROUND: (f32, f64) = signed(16_777_217);
const NEGATIVE: (f32, f64) = signed(-16_777_217);
const WIDE: (f32, f64) = unsigned(u128::MAX);
const ZERO: (f32, f64) = narrowed(-0.0);
const LARGE: (f32, f64) = narrowed(f64::MAX);
fn main() {
    assert_eq!(ROUND, (16_777_216.0, 16_777_217.0));
    assert_eq!(NEGATIVE, (-16_777_216.0, -16_777_217.0));
    assert_eq!(WIDE.0, f32::INFINITY);
    assert_eq!(WIDE.1.to_bits(), 0x47f0000000000000);
    assert_eq!(ZERO.0.to_bits(), 0x80000000);
    assert_eq!(ZERO.1.to_bits(), 0x8000000000000000);
    assert_eq!(LARGE, (f32::INFINITY, f64::INFINITY));
    let value = std::hint::black_box(16_777_217i128);
    assert_eq!(signed(value), ROUND);
}
