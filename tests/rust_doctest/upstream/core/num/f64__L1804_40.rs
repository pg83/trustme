// Extracted from library/core/src/num/f64.rs:1804
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f64;

    let a: f64 = 7.0;
    let b = 4.0;
    assert_eq!(f64::math::div_euclid(a, b), 1.0); // 7.0 > 4.0 * 1.0
    assert_eq!(f64::math::div_euclid(-a, b), -2.0); // -7.0 >= 4.0 * -2.0
    assert_eq!(f64::math::div_euclid(a, -b), -1.0); // 7.0 >= -4.0 * -1.0
    assert_eq!(f64::math::div_euclid(-a, -b), 2.0); // -7.0 >= -4.0 * 2.0
}
