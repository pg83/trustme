#![feature(f16, f128)]
fn main() {
    let positive = 7.5f64;
    let negative = -7.5f64;
    let divisor = 2.0f64;
    let zero = -0.0f64;
    let half = 7.5f16;
    let single = 7.5f32;
    let quad = 7.5f128;
    let results = (positive % divisor, negative % divisor, zero % divisor);
    let other_widths = (half % 2.0f16, single % 2.0f32, quad % 2.0f128);
    assert_eq!(results.0, 1.5);
    assert_eq!(results.1, -1.5);
    assert_eq!(results.2.to_bits(), 1u64 << 63);
    assert_eq!(other_widths, (1.5f16, 1.5f32, 1.5f128));
}
