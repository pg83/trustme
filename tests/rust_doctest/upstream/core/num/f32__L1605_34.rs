// Extracted from library/core/src/num/f32.rs:1605
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f32;
    
    let f = 3.01_f32;
    let g = 4.0_f32;
    
    assert_eq!(f32::math::ceil(f), 4.0);
    assert_eq!(f32::math::ceil(g), 4.0);
}
