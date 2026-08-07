// Extracted from library/core/src/num/f32.rs:1898
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f32;

    let positive = 4.0_f32;
    let negative = -4.0_f32;
    let negative_zero = -0.0_f32;

    assert_eq!(f32::math::sqrt(positive), 2.0);
    assert!(f32::math::sqrt(negative).is_nan());
    assert_eq!(f32::math::sqrt(negative_zero), negative_zero);
}
