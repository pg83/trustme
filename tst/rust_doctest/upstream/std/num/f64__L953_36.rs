// Extracted from library/std/src/num/f64.rs:953
#![allow(unused)]
fn main() {
    assert_eq!((-1.0_f64).ln_1p(), f64::NEG_INFINITY);
    assert!((-2.0_f64).ln_1p().is_nan());
}
