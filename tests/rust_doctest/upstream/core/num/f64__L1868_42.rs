// Extracted from library/core/src/num/f64.rs:1868
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f64;
    
    let x = 2.0_f64;
    let abs_difference = (f64::math::powi(x, 2) - (x * x)).abs();
    assert!(abs_difference <= 1e-6);
    
    assert_eq!(f64::math::powi(f64::NAN, 0), 1.0);
}
