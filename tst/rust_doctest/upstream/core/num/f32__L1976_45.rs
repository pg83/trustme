// Extracted from library/core/src/num/f32.rs:1976
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f32;

    let x = 8.0f32;

    // x^(1/3) - 2 == 0
    let abs_difference = (f32::math::cbrt(x) - 2.0).abs();

    assert!(abs_difference <= f32::EPSILON);
}
