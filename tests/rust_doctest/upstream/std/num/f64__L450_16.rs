// Extracted from library/std/src/num/f64.rs:450
#![allow(unused)]
fn main() {
    assert_eq!(0_f64.ln(), f64::NEG_INFINITY);
    assert!((-42_f64).ln().is_nan());
}
