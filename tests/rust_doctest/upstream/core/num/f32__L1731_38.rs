// Extracted from library/core/src/num/f32.rs:1731
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f32;
    
    let x = 3.6_f32;
    let y = -3.6_f32;
    let abs_difference_x = (f32::math::fract(x) - 0.6).abs();
    let abs_difference_y = (f32::math::fract(y) - (-0.6)).abs();
    
    assert!(abs_difference_x <= f32::EPSILON);
    assert!(abs_difference_y <= f32::EPSILON);
}
