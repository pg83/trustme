// Extracted from library/core/src/num/f32.rs:1806
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f32;
    
    let a: f32 = 7.0;
    let b = 4.0;
    assert_eq!(f32::math::div_euclid(a, b), 1.0); // 7.0 > 4.0 * 1.0
    assert_eq!(f32::math::div_euclid(-a, b), -2.0); // -7.0 >= 4.0 * -2.0
    assert_eq!(f32::math::div_euclid(a, -b), -1.0); // 7.0 >= -4.0 * -1.0
    assert_eq!(f32::math::div_euclid(-a, -b), 2.0); // -7.0 >= -4.0 * 2.0
}
