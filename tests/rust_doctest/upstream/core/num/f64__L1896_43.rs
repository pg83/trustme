// Extracted from library/core/src/num/f64.rs:1896
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f64;

    let positive = 4.0_f64;
    let negative = -4.0_f64;
    let negative_zero = -0.0_f64;

    assert_eq!(f64::math::sqrt(positive), 2.0);
    assert!(f64::math::sqrt(negative).is_nan());
    assert_eq!(f64::math::sqrt(negative_zero), negative_zero);
}
