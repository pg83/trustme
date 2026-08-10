// Extracted from library/core/src/num/f64.rs:1393
#![allow(unused)]
fn main() {
    assert!((-3.0f64).clamp(-2.0, 1.0) == -2.0);
    assert!((0.0f64).clamp(-2.0, 1.0) == 0.0);
    assert!((2.0f64).clamp(-2.0, 1.0) == 1.0);
    assert!((f64::NAN).clamp(-2.0, 1.0).is_nan());
}
