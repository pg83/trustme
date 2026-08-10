// Extracted from library/core/src/num/f64.rs:1603
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f64;

    let f = 3.01_f64;
    let g = 4.0_f64;

    assert_eq!(f64::math::ceil(f), 4.0);
    assert_eq!(f64::math::ceil(g), 4.0);
}
