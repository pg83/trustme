// Extracted from library/core/src/num/f32.rs:1929
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f32;

    let x = 3.0f32;
    let y = -3.0f32;

    let abs_difference_x = (f32::math::abs_sub(x, 1.0) - 2.0).abs();
    let abs_difference_y = (f32::math::abs_sub(y, 1.0) - 0.0).abs();

    assert!(abs_difference_x <= f32::EPSILON);
    assert!(abs_difference_y <= f32::EPSILON);
}
