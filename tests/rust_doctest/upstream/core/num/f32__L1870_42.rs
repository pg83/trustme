// Extracted from library/core/src/num/f32.rs:1870
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f32;
    
    let x = 2.0_f32;
    let abs_difference = (f32::math::powi(x, 2) - (x * x)).abs();
    assert!(abs_difference <= 1e-5);
    
    assert_eq!(f32::math::powi(f32::NAN, 0), 1.0);
}
