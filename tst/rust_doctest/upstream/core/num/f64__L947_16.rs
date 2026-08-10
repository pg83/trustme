// Extracted from library/core/src/num/f64.rs:947
#![allow(unused)]
#![feature(float_minimum_maximum)]
fn main() {
    let x = 1.0_f64;
    let y = 2.0_f64;

    assert_eq!(x.maximum(y), y);
    assert!(x.maximum(f64::NAN).is_nan());
}
