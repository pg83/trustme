// Extracted from library/core/src/num/f64.rs:588
#![allow(unused)]
fn main() {
    let min = f64::MIN_POSITIVE; // 2.2250738585072014e-308_f64
    let max = f64::MAX;
    let lower_than_min = 1.0e-308_f64;
    let zero = 0.0_f64;

    assert!(!min.is_subnormal());
    assert!(!max.is_subnormal());

    assert!(!zero.is_subnormal());
    assert!(!f64::NAN.is_subnormal());
    assert!(!f64::INFINITY.is_subnormal());
    // Values between `0` and `min` are Subnormal.
    assert!(lower_than_min.is_subnormal());
}
