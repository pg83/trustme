// Extracted from library/core/src/num/f64.rs:1667
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    use core::f64;

    let f = 3.3_f64;
    let g = -3.3_f64;
    let h = 3.5_f64;
    let i = 4.5_f64;

    assert_eq!(f64::math::round_ties_even(f), 3.0);
    assert_eq!(f64::math::round_ties_even(g), -3.0);
    assert_eq!(f64::math::round_ties_even(h), 4.0);
    assert_eq!(f64::math::round_ties_even(i), 4.0);
}
