// Extracted from library/core/src/num/f64.rs:615
#![allow(unused)]
fn main() {
    let min = f64::MIN_POSITIVE; // 2.2250738585072014e-308f64
    let max = f64::MAX;
    let lower_than_min = 1.0e-308_f64;
    let zero = 0.0f64;

    assert!(min.is_normal());
    assert!(max.is_normal());

    assert!(!zero.is_normal());
    assert!(!f64::NAN.is_normal());
    assert!(!f64::INFINITY.is_normal());
    // Values between `0` and `min` are Subnormal.
    assert!(!lower_than_min.is_normal());
}
