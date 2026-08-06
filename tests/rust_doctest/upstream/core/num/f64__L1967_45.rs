// Extracted from library/core/src/num/f64.rs:1967
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f64;
    
    let x = 8.0_f64;
    
    // x^(1/3) - 2 == 0
    let abs_difference = (f64::math::cbrt(x) - 2.0).abs();
    
    assert!(abs_difference < 1e-10);
}
