// Extracted from library/std/src/num/f64.rs:520
#![allow(unused)]
fn main() {
    assert_eq!(0_f64.log2(), f64::NEG_INFINITY);
    assert!((-42_f64).log2().is_nan());
}
