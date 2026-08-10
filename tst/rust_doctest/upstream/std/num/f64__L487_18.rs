// Extracted from library/std/src/num/f64.rs:487
#![allow(unused)]
fn main() {
    assert_eq!(0_f64.log(10.0), f64::NEG_INFINITY);
    assert!((-42_f64).log(10.0).is_nan());
}
