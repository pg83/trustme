// Extracted from library/core/src/num/f32.rs:1634
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f32;

    let f = 3.3_f32;
    let g = -3.3_f32;
    let h = -3.7_f32;
    let i = 3.5_f32;
    let j = 4.5_f32;

    assert_eq!(f32::math::round(f), 3.0);
    assert_eq!(f32::math::round(g), -3.0);
    assert_eq!(f32::math::round(h), -4.0);
    assert_eq!(f32::math::round(i), 4.0);
    assert_eq!(f32::math::round(j), 5.0);
}
