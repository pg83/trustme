// Extracted from library/core/src/num/f64.rs:1632
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f64;

    let f = 3.3_f64;
    let g = -3.3_f64;
    let h = -3.7_f64;
    let i = 3.5_f64;
    let j = 4.5_f64;

    assert_eq!(f64::math::round(f), 3.0);
    assert_eq!(f64::math::round(g), -3.0);
    assert_eq!(f64::math::round(h), -4.0);
    assert_eq!(f64::math::round(i), 4.0);
    assert_eq!(f64::math::round(j), 5.0);
}
