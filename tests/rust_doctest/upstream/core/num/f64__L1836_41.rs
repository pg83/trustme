// Extracted from library/core/src/num/f64.rs:1836
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f64;
    
    let a: f64 = 7.0;
    let b = 4.0;
    assert_eq!(f64::math::rem_euclid(a, b), 3.0);
    assert_eq!(f64::math::rem_euclid(-a, b), 1.0);
    assert_eq!(f64::math::rem_euclid(a, -b), 3.0);
    assert_eq!(f64::math::rem_euclid(-a, -b), 1.0);
    // limitation due to round-off error
    assert!(f64::math::rem_euclid(-f64::EPSILON, 3.0) != 0.0);
}
