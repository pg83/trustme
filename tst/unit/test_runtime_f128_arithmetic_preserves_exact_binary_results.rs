#![feature(f128)]
#[inline(never)]
fn calculate(a: f128, b: f128) -> [u128; 4] {
    [(a + b).to_bits(), (a - b).to_bits(), (a * b).to_bits(), (a / b).to_bits()]
}
fn main() {
    let a = std::hint::black_box(7.5f128);
    let b = std::hint::black_box(2.5f128);
    assert_eq!(calculate(a, b), [10.0f128.to_bits(), 5.0f128.to_bits(), 18.75f128.to_bits(), 3.0f128.to_bits()]);
    assert_eq!(calculate(-a, b), [(-5.0f128).to_bits(), (-10.0f128).to_bits(), (-18.75f128).to_bits(), (-3.0f128).to_bits()]);
}
