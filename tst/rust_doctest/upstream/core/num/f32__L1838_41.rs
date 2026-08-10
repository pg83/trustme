// Extracted from library/core/src/num/f32.rs:1838
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f32;

    let a: f32 = 7.0;
    let b = 4.0;
    assert_eq!(f32::math::rem_euclid(a, b), 3.0);
    assert_eq!(f32::math::rem_euclid(-a, b), 1.0);
    assert_eq!(f32::math::rem_euclid(a, -b), 3.0);
    assert_eq!(f32::math::rem_euclid(-a, -b), 1.0);
    // limitation due to round-off error
    assert!(f32::math::rem_euclid(-f32::EPSILON, 3.0) != 0.0);
}
