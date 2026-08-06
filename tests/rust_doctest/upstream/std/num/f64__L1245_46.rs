// Extracted from library/std/src/num/f64.rs:1245
#![allow(unused)]
#![feature(float_erf)]
fn main() {
    let x: f64 = 0.123;
    
    let one = x.erf() + x.erfc();
    let abs_difference = (one - 1.0).abs();
    
    assert!(abs_difference <= f64::EPSILON);
}
