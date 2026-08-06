// Extracted from library/core/src/num/f64.rs:1927
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f64;
    
    let x = 3.0_f64;
    let y = -3.0_f64;
    
    let abs_difference_x = (f64::math::abs_sub(x, 1.0) - 2.0).abs();
    let abs_difference_y = (f64::math::abs_sub(y, 1.0) - 0.0).abs();
    
    assert!(abs_difference_x < 1e-10);
    assert!(abs_difference_y < 1e-10);
}
