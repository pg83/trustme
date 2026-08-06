// Extracted from library/core/src/num/f64.rs:1729
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f64;
    
    let x = 3.6_f64;
    let y = -3.6_f64;
    let abs_difference_x = (f64::math::fract(x) - 0.6).abs();
    let abs_difference_y = (f64::math::fract(y) - (-0.6)).abs();
    
    assert!(abs_difference_x < 1e-10);
    assert!(abs_difference_y < 1e-10);
}
