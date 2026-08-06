// Extracted from library/core/src/num/f64.rs:1698
#![allow(unused)]
#![feature(core_float_math)]
fn main() {
    
    use core::f64;
    
    let f = 3.7_f64;
    let g = 3.0_f64;
    let h = -3.7_f64;
    
    assert_eq!(f64::math::trunc(f), 3.0);
    assert_eq!(f64::math::trunc(g), 3.0);
    assert_eq!(f64::math::trunc(h), -3.0);
}
