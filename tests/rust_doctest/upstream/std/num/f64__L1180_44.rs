// Extracted from library/std/src/num/f64.rs:1180
#![allow(unused)]
#![feature(float_gamma)]
fn main() {
    let x = 2.0f64;
    
    let abs_difference = (x.ln_gamma().0 - 0.0).abs();
    
    assert!(abs_difference <= f64::EPSILON);
}
