// Extracted from library/std/src/num/f64.rs:1151
#![allow(unused)]
#![feature(float_gamma)]
fn main() {
    let x = 5.0f64;
    
    let abs_difference = (x.gamma() - 24.0).abs();
    
    assert!(abs_difference <= f64::EPSILON);
}
