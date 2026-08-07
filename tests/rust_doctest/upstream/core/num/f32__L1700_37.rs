// Extracted from library/core/src/num/f32.rs:1700
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f32;

    let f = 3.7_f32;
    let g = 3.0_f32;
    let h = -3.7_f32;

    assert_eq!(f32::math::trunc(f), 3.0);
    assert_eq!(f32::math::trunc(g), 3.0);
    assert_eq!(f32::math::trunc(h), -3.0);
}
